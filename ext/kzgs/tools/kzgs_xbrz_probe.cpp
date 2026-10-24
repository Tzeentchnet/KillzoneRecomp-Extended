// SPDX-License-Identifier: GPL-3.0+

#include "kz_xbrz.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <new>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Microsoft::WRL::ComPtr;

	constexpr std::uint32_t kScaleFactor = 2;
	constexpr std::uint32_t kMaxInputDimension = 1024;

	struct Image
	{
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::vector<std::uint32_t> pixels;
	};

	struct Options
	{
		std::filesystem::path input;
		std::filesystem::path output;
		bool overwrite = false;
		bool dry_run = false;
	};

	struct InputItem
	{
		std::filesystem::path source;
		std::filesystem::path relative;
	};

	struct ProbeSummary
	{
		std::size_t written = 0;
		std::size_t eligible = 0;
		std::size_t skipped_existing = 0;
		std::size_t skipped_ineligible = 0;
		std::size_t failures = 0;
	};

	enum class LoadResult
	{
		Success,
		Ineligible,
		Failure,
	};

	std::wstring hresultMessage(std::wstring_view operation, HRESULT result)
	{
		std::wostringstream stream;
		stream << operation << L" failed (HRESULT 0x" << std::hex << std::setw(8) << std::setfill(L'0')
			   << static_cast<unsigned long>(result) << L")";
		return stream.str();
	}

	std::wstring win32Message(std::wstring_view operation, DWORD error)
	{
		std::wostringstream stream;
		stream << operation << L" failed (Win32 error " << std::dec << error << L")";
		return stream.str();
	}

	std::wstring widenAscii(const char* text)
	{
		std::wstring result;
		while (*text != '\0')
			result.push_back(static_cast<unsigned char>(*text++));
		return result;
	}

	bool isPng(const std::filesystem::path& path)
	{
		std::wstring extension = path.extension().wstring();
		std::transform(extension.begin(), extension.end(), extension.begin(),
			[](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
		return extension == L".png";
	}

	bool pathComponentEquals(const std::filesystem::path& left, const std::filesystem::path& right)
	{
		return _wcsicmp(left.c_str(), right.c_str()) == 0;
	}

	bool isSameOrChildPath(const std::filesystem::path& child, const std::filesystem::path& parent)
	{
		auto child_it = child.begin();
		for (auto parent_it = parent.begin(); parent_it != parent.end(); ++parent_it, ++child_it)
		{
			if (child_it == child.end() || !pathComponentEquals(*child_it, *parent_it))
				return false;
		}
		return true;
	}

	bool normalizePath(const std::filesystem::path& path, bool must_exist, std::filesystem::path* output,
		std::wstring* error)
	{
		std::error_code code;
		*output = must_exist ? std::filesystem::canonical(path, code) : std::filesystem::weakly_canonical(path, code);
		if (!code)
			return true;

		*error = L"cannot resolve '" + path.wstring() + L"': " + widenAscii(code.message().c_str());
		return false;
	}

	LoadResult loadPng(IWICImagingFactory* factory, const std::filesystem::path& filename, Image* image,
		std::wstring* error)
	{
		ComPtr<IWICBitmapDecoder> decoder;
		HRESULT result = factory->CreateDecoderFromFilename(filename.c_str(), nullptr, GENERIC_READ,
			WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
		if (FAILED(result))
		{
			*error = hresultMessage(L"opening PNG", result);
			return LoadResult::Failure;
		}

		ComPtr<IWICBitmapFrameDecode> frame;
		result = decoder->GetFrame(0, frame.GetAddressOf());
		if (FAILED(result))
		{
			*error = hresultMessage(L"reading PNG frame", result);
			return LoadResult::Failure;
		}

		UINT width = 0;
		UINT height = 0;
		result = frame->GetSize(&width, &height);
		if (FAILED(result) || width == 0 || height == 0)
		{
			*error = FAILED(result) ? hresultMessage(L"reading PNG dimensions", result) : L"PNG has empty dimensions";
			return LoadResult::Failure;
		}
		if (width > kMaxInputDimension || height > kMaxInputDimension)
		{
			std::wostringstream stream;
			stream << L"dimensions " << width << L"x" << height << L" exceed the "
				   << kMaxInputDimension << L"x" << kMaxInputDimension << L" probe limit";
			*error = stream.str();
			return LoadResult::Ineligible;
		}

		ComPtr<IWICFormatConverter> converter;
		result = factory->CreateFormatConverter(converter.GetAddressOf());
		if (FAILED(result))
		{
			*error = hresultMessage(L"creating PNG format converter", result);
			return LoadResult::Failure;
		}
		result = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr,
			0.0, WICBitmapPaletteTypeCustom);
		if (FAILED(result))
		{
			*error = hresultMessage(L"converting PNG to BGRA8", result);
			return LoadResult::Failure;
		}

		const UINT stride = width * sizeof(std::uint32_t);
		const UINT buffer_size = stride * height;
		image->width = width;
		image->height = height;
		image->pixels.resize(static_cast<std::size_t>(width) * height);
		result = converter->CopyPixels(nullptr, stride, buffer_size,
			reinterpret_cast<BYTE*>(image->pixels.data()));
		if (FAILED(result))
		{
			*error = hresultMessage(L"decoding PNG pixels", result);
			return LoadResult::Failure;
		}

		return LoadResult::Success;
	}

	bool encodePng(IWICImagingFactory* factory, const std::filesystem::path& filename, const Image& image,
		std::wstring* error)
	{
		ComPtr<IWICStream> stream;
		HRESULT result = factory->CreateStream(stream.GetAddressOf());
		if (FAILED(result))
		{
			*error = hresultMessage(L"creating PNG stream", result);
			return false;
		}
		result = stream->InitializeFromFilename(filename.c_str(), GENERIC_WRITE);
		if (FAILED(result))
		{
			*error = hresultMessage(L"opening PNG output", result);
			return false;
		}

		ComPtr<IWICBitmapEncoder> encoder;
		result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
		if (FAILED(result))
		{
			*error = hresultMessage(L"creating PNG encoder", result);
			return false;
		}
		result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
		if (FAILED(result))
		{
			*error = hresultMessage(L"initializing PNG encoder", result);
			return false;
		}

		ComPtr<IWICBitmapFrameEncode> frame;
		ComPtr<IPropertyBag2> properties;
		result = encoder->CreateNewFrame(frame.GetAddressOf(), properties.GetAddressOf());
		if (FAILED(result))
		{
			*error = hresultMessage(L"creating PNG frame", result);
			return false;
		}
		result = frame->Initialize(properties.Get());
		if (FAILED(result))
		{
			*error = hresultMessage(L"initializing PNG frame", result);
			return false;
		}
		result = frame->SetSize(image.width, image.height);
		if (FAILED(result))
		{
			*error = hresultMessage(L"setting PNG dimensions", result);
			return false;
		}

		WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
		result = frame->SetPixelFormat(&format);
		if (FAILED(result) || !IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA))
		{
			*error = FAILED(result) ? hresultMessage(L"setting PNG pixel format", result) :
									 L"PNG encoder rejected the BGRA8 pixel format";
			return false;
		}

		const UINT stride = image.width * sizeof(std::uint32_t);
		const UINT buffer_size = stride * image.height;
		result = frame->WritePixels(image.height, stride, buffer_size,
			reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(image.pixels.data())));
		if (FAILED(result))
		{
			*error = hresultMessage(L"writing PNG pixels", result);
			return false;
		}
		result = frame->Commit();
		if (FAILED(result))
		{
			*error = hresultMessage(L"committing PNG frame", result);
			return false;
		}
		result = encoder->Commit();
		if (FAILED(result))
		{
			*error = hresultMessage(L"committing PNG", result);
			return false;
		}
		return true;
	}

	bool savePngAtomically(IWICImagingFactory* factory, const std::filesystem::path& filename, const Image& image,
		bool overwrite, std::wstring* error)
	{
		std::error_code code;
		std::filesystem::create_directories(filename.parent_path(), code);
		if (code)
		{
			*error = L"cannot create output directory: " + widenAscii(code.message().c_str());
			return false;
		}

		std::filesystem::path temporary = filename;
		temporary += L".xbrz.tmp";
		std::filesystem::remove(temporary, code);
		if (code)
		{
			*error = L"cannot remove stale temporary output: " + widenAscii(code.message().c_str());
			return false;
		}

		if (!encodePng(factory, temporary, image, error))
		{
			std::filesystem::remove(temporary, code);
			return false;
		}

		const DWORD flags = MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0);
		if (!MoveFileExW(temporary.c_str(), filename.c_str(), flags))
		{
			*error = win32Message(L"publishing PNG output", GetLastError());
			std::filesystem::remove(temporary, code);
			return false;
		}
		return true;
	}

	bool collectInputs(const std::filesystem::path& input, std::vector<InputItem>* items, std::wstring* error)
	{
		if (std::filesystem::is_regular_file(input))
		{
			if (!isPng(input))
			{
				*error = L"input file must have a .png extension";
				return false;
			}
			items->push_back({input, input.filename()});
			return true;
		}
		if (!std::filesystem::is_directory(input))
		{
			*error = L"input is neither a PNG file nor a directory";
			return false;
		}

		for (const std::filesystem::directory_entry& entry :
			std::filesystem::recursive_directory_iterator(input))
		{
			if (!isPng(entry.path()))
				continue;
			if (entry.is_symlink())
			{
				std::wcerr << L"skip symlink: " << entry.path().wstring() << L"\n";
				continue;
			}
			if (entry.is_regular_file())
				items->push_back({entry.path(), std::filesystem::relative(entry.path(), input)});
		}
		std::sort(items->begin(), items->end(), [](const InputItem& left, const InputItem& right) {
			return _wcsicmp(left.relative.c_str(), right.relative.c_str()) < 0;
		});
		if (!items->empty())
			return true;

		*error = L"input contains no PNG files";
		return false;
	}

	void processOne(IWICImagingFactory* factory, const InputItem& item, const Options& options, ProbeSummary* summary)
	{
		const std::filesystem::path destination = options.output / item.relative;
		if (isSameOrChildPath(item.source, destination) && isSameOrChildPath(destination, item.source))
		{
			std::wcerr << L"fail: input and output resolve to the same file: " << item.source.wstring() << L"\n";
			summary->failures++;
			return;
		}
		if (!options.overwrite && std::filesystem::exists(destination))
		{
			std::wcout << L"skip existing: " << destination.wstring() << L"\n";
			summary->skipped_existing++;
			return;
		}

		Image source;
		std::wstring error;
		const LoadResult load_result = loadPng(factory, item.source, &source, &error);
		if (load_result == LoadResult::Ineligible)
		{
			std::wcout << L"skip ineligible: " << item.source.wstring() << L" (" << error << L")\n";
			summary->skipped_ineligible++;
			return;
		}
		if (load_result == LoadResult::Failure)
		{
			std::wcerr << L"fail: " << item.source.wstring() << L": " << error << L"\n";
			summary->failures++;
			return;
		}

		KzXbrzImageSize output_size;
		const KzXbrzScaleResult size_result =
			kzXbrzGetOutputSize(source.width, source.height, kScaleFactor, &output_size);
		if (size_result != KzXbrzScaleResult::Success)
		{
			std::wcerr << L"fail: " << item.source.wstring() << L": output sizing returned "
					   << widenAscii(kzXbrzScaleResultName(size_result)) << L"\n";
			summary->failures++;
			return;
		}
		if (options.dry_run)
		{
			std::wcout << L"eligible: " << item.source.wstring() << L" (" << source.width << L"x" << source.height
					   << L" -> " << output_size.width << L"x" << output_size.height << L")\n";
			summary->eligible++;
			return;
		}

		Image output;
		output.width = output_size.width;
		output.height = output_size.height;
		output.pixels.resize(output_size.pixels);
		const KzXbrzScaleResult scale_result = kzXbrzScaleRgba(source.pixels.data(), source.pixels.size(),
			source.width, source.height, kScaleFactor, output.pixels.data(), output.pixels.size());
		if (scale_result != KzXbrzScaleResult::Success)
		{
			std::wcerr << L"fail: " << item.source.wstring() << L": scaling returned "
					   << widenAscii(kzXbrzScaleResultName(scale_result)) << L"\n";
			summary->failures++;
			return;
		}
		if (!savePngAtomically(factory, destination, output, options.overwrite, &error))
		{
			std::wcerr << L"fail: " << destination.wstring() << L": " << error << L"\n";
			summary->failures++;
			return;
		}

		std::wcout << L"wrote: " << destination.wstring() << L" (" << source.width << L"x" << source.height
				   << L" -> " << output.width << L"x" << output.height << L")\n";
		summary->written++;
	}

	int runProbe(IWICImagingFactory* factory, Options options)
	{
		std::wstring error;
		if (!normalizePath(options.input, true, &options.input, &error))
		{
			std::wcerr << L"error: " << error << L"\n";
			return 2;
		}
		if (!normalizePath(options.output, false, &options.output, &error))
		{
			std::wcerr << L"error: " << error << L"\n";
			return 2;
		}
		if (std::filesystem::exists(options.output) && !std::filesystem::is_directory(options.output))
		{
			std::wcerr << L"error: output must be a directory\n";
			return 2;
		}
		if (std::filesystem::is_directory(options.input) && isSameOrChildPath(options.output, options.input))
		{
			std::wcerr << L"error: output directory must not be the input directory or one of its descendants\n";
			return 2;
		}

		std::vector<InputItem> items;
		if (!collectInputs(options.input, &items, &error))
		{
			std::wcerr << L"error: " << error << L"\n";
			return 2;
		}

		ProbeSummary summary;
		for (const InputItem& item : items)
			processOne(factory, item, options, &summary);

		std::wcout << L"xBRZ probe summary: selected=" << items.size() << L", written=" << summary.written
				   << L", eligible=" << summary.eligible << L", existing=" << summary.skipped_existing
				   << L", ineligible=" << summary.skipped_ineligible << L", failures=" << summary.failures << L"\n";
		if (summary.failures != 0)
			return 1;
		if (summary.written + summary.eligible + summary.skipped_existing == 0)
			return 2;
		return 0;
	}

	int runSelfTest(IWICImagingFactory* factory, const std::filesystem::path& root)
	{
		const std::filesystem::path input_directory = root / L"input";
		const std::filesystem::path output_directory = root / L"output";
		const std::filesystem::path input_file = input_directory / L"synthetic.png";
		const std::filesystem::path output_file = output_directory / L"synthetic.png";

		Image source;
		source.width = 2;
		source.height = 2;
		source.pixels.assign(4, 0x40112233u);

		std::wstring error;
		if (!savePngAtomically(factory, input_file, source, true, &error))
		{
			std::wcerr << L"self-test: cannot create input: " << error << L"\n";
			return 1;
		}

		Options options;
		options.input = input_file;
		options.output = output_directory;
		options.overwrite = true;
		if (runProbe(factory, std::move(options)) != 0)
			return 1;

		Image result;
		if (loadPng(factory, output_file, &result, &error) != LoadResult::Success)
		{
			std::wcerr << L"self-test: cannot read output: " << error << L"\n";
			return 1;
		}
		if (result.width != 4 || result.height != 4 ||
			!std::all_of(result.pixels.begin(), result.pixels.end(),
				[](std::uint32_t pixel) { return pixel == 0x40112233u; }))
		{
			std::wcerr << L"self-test: output dimensions, color, or alpha do not match\n";
			return 1;
		}

		Image boundary;
		boundary.width = kMaxInputDimension;
		boundary.height = 1;
		boundary.pixels.assign(kMaxInputDimension, 0xff123456u);
		const std::filesystem::path boundary_file = input_directory / L"boundary.png";
		if (!savePngAtomically(factory, boundary_file, boundary, true, &error))
		{
			std::wcerr << L"self-test: cannot create boundary input: " << error << L"\n";
			return 1;
		}
		Image boundary_result;
		if (loadPng(factory, boundary_file, &boundary_result, &error) != LoadResult::Success ||
			boundary_result.width != kMaxInputDimension || boundary_result.height != 1)
		{
			std::wcerr << L"self-test: 1024-pixel boundary input was not accepted\n";
			return 1;
		}

		Image oversized;
		oversized.width = kMaxInputDimension + 1;
		oversized.height = 1;
		oversized.pixels.assign(kMaxInputDimension + 1, 0xff123456u);
		const std::filesystem::path oversized_file = input_directory / L"oversized.png";
		if (!savePngAtomically(factory, oversized_file, oversized, true, &error))
		{
			std::wcerr << L"self-test: cannot create oversized input: " << error << L"\n";
			return 1;
		}
		Image oversized_result;
		if (loadPng(factory, oversized_file, &oversized_result, &error) != LoadResult::Ineligible)
		{
			std::wcerr << L"self-test: 1025-pixel input was not rejected\n";
			return 1;
		}

		std::wcout << L"kzgs xBRZ probe: PASS\n";
		return 0;
	}

	void printUsage()
	{
		std::wcout
			<< L"Usage:\n"
			<< L"  kzgs_xbrz_probe --input <png-or-directory> --output <directory> [--overwrite] [--dry-run]\n"
			<< L"  kzgs_xbrz_probe --self-test <work-directory>\n\n"
			<< L"The probe writes 2x PNGs with the same relative filenames as PCSX2 dumps.\n"
			<< L"Inputs larger than 1024 pixels in either dimension are reported and skipped.\n";
	}

	bool parseOptions(int argc, wchar_t** argv, Options* options)
	{
		for (int index = 1; index < argc; index++)
		{
			const std::wstring_view argument(argv[index]);
			if (argument == L"--input" || argument == L"--output")
			{
				if (++index >= argc)
				return false;
				if (argument == L"--input")
					options->input = argv[index];
				else
					options->output = argv[index];
			}
			else if (argument == L"--overwrite")
			{
				options->overwrite = true;
			}
			else if (argument == L"--dry-run")
			{
				options->dry_run = true;
			}
			else
			{
				return false;
			}
		}
		return !options->input.empty() && !options->output.empty();
	}
}

int wmain(int argc, wchar_t** argv)
{
	if (argc == 2 && (std::wcscmp(argv[1], L"--help") == 0 || std::wcscmp(argv[1], L"-h") == 0))
	{
		printUsage();
		return 0;
	}

	const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	const bool uninitialize_com = SUCCEEDED(com_result);
	if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE)
	{
		std::wcerr << L"error: " << hresultMessage(L"initializing COM", com_result) << L"\n";
		return 1;
	}

	int exit_code = 1;
	try
	{
		ComPtr<IWICImagingFactory> factory;
		const HRESULT factory_result = CoCreateInstance(
			CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
		if (FAILED(factory_result))
		{
			std::wcerr << L"error: " << hresultMessage(L"creating WIC factory", factory_result) << L"\n";
		}
		else if (argc == 3 && std::wcscmp(argv[1], L"--self-test") == 0)
		{
			exit_code = runSelfTest(factory.Get(), argv[2]);
		}
		else
		{
			Options options;
			if (!parseOptions(argc, argv, &options))
			{
				printUsage();
				exit_code = 2;
			}
			else
			{
				exit_code = runProbe(factory.Get(), std::move(options));
			}
		}
	}
	catch (const std::bad_alloc&)
	{
		std::wcerr << L"error: insufficient memory while processing probe textures\n";
		exit_code = 1;
	}
	catch (const std::filesystem::filesystem_error& error)
	{
		std::wcerr << L"error: filesystem operation failed: " << widenAscii(error.what()) << L"\n";
		exit_code = 1;
	}
	catch (const std::exception& error)
	{
		std::wcerr << L"error: unexpected failure: " << widenAscii(error.what()) << L"\n";
		exit_code = 1;
	}

	if (uninitialize_com)
		CoUninitialize();
	return exit_code;
}
