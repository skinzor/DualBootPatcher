/*
 * Copyright (C) 2014  Andrew Gunnerson <andrewgunnerson@gmail.com>
 *
 * This file is part of MultiBootPatcher
 *
 * MultiBootPatcher is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MultiBootPatcher is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MultiBootPatcher.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string>
#include <unordered_map>

#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <getopt.h>

#include <libmbpio/directory.h>
#include <libmbpio/error.h>
#include <libmbpio/path.h>

#include <libmbp/bootimage.h>
#include <libmbp/logging.h>
#include <libmbp/patchererror.h>


typedef std::unique_ptr<std::FILE, int (*)(std::FILE *)> file_ptr;


static const char MainUsage[] =
    "Usage: bootimgtool <command> [<args>]\n"
    "\n"
    "Available commands:\n"
    "  unpack         Unpack a boot image\n"
    "  pack           Assemble boot image from unpacked files\n"
    "\n"
    "Pass -h/--help as a argument to a command to see it's available options.\n";

static const char UnpackUsage[] =
    "Usage: bootimgtool unpack [input file] [options]\n"
    "\n"
    "Options:\n"
    "  -o, --output [output directory]\n"
    "                  Output directory (current directory if unspecified)\n"
    "  -p, --prefix [prefix]\n"
    "                  Prefix to prepend to output filenames\n"
    "  -n, --noprefix  Do not prepend a prefix to the item filenames\n"
    "  --output-[item] [item path]\n"
    "                  Custom path for a particular item\n"
    "\n"
    "The following items are extracted from the boot image. These files contain\n"
    "all of the information necessary to recreate an identical boot image.\n"
    "\n"
    "  cmdline         Kernel command line                           [ABLS]\n"
    "  board           Board name field in the header                [ABL ]\n"
    "  base            Base address for offsets                      [ABL ]\n"
    "  kernel_offset   Address offset of the kernel image            [ABLS]\n"
    "  ramdisk_offset  Address offset of the ramdisk image           [ABLS]\n"
    "  second_offset   Address offset of the second bootloader image [ABL ]\n"
    "  tags_offset     Address offset of the kernel tags image       [ABL ]\n"
    "  ipl_address     Address of the ipl image                      [   S]\n"
    "  rpm_address     Address of the rpm image                      [   S]\n"
    "  appsbl_address  Address of the appsbl image                   [   S]\n"
    "  entrypoint      Address of the entry point                    [   S]\n"
    "  page_size       Page size                                     [ABL ]\n"
    "  kernel`         Kernel image                                  [ABLS]\n"
    "  ramdisk         Ramdisk image                                 [ABLS]\n"
    "  second          Second bootloader image                       [ABL ]\n"
    "  dt              Device tree image                             [ABL ]\n"
    "  ipl             Ipl image                                     [   S]\n"
    "  rpm             Rpm image                                     [   S]\n"
    "  appsbl          Appsbl image                                  [   S]\n"
    "  sin             Sin image                                     [   S]\n"
    "  sinhdr          Sin header                                    [   S]\n"
    "\n"
    "Legend:\n"
    "  [A B L S]\n"
    "   | | | `- Used by Sony ELF boot images\n"
    "   | | `- Used by Loki'd Android boot images\n"
    "   | `- Used by bump'd Android boot images\n"
    "   `- Used by plain Android boot images\n"
    "\n"
    "Output files:\n"
    "\n"
    "By default, the items are unpacked to the following path:\n"
    "\n"
    "    [output directory/[prefix]-[item]\n"
    "\n"
    "If a prefix wasn't specified, then the input filename is used as a prefix.\n"
    "(eg. \"bootimgtool unpack boot.img -o /tmp\" will unpack /tmp/boot.img-kernel,\n"
    "..., etc.). If the -n/--noprefix option was specified, then the items are\n"
    "unpacked to the following path:\n"
    "\n"
    "    [output directory]/[item]\n"
    "\n"
    "If the --output-[item]=[item path] option is specified, then that particular\n"
    "item is unpacked to the specified [item path].\n"
    "\n"
    "Examples:\n"
    "\n"
    "1. Plain ol' unpacking (just make this thing work!). This extracts boot.img to\n"
    "   boot.img-cmdline, boot.img-base, ...\n"
    "\n"
    "        bootimgtool unpack boot.img\n"
    "\n"
    "2. Unpack to a different directory, but put the kernel in /tmp/\n"
    "\n"
    "        bootimgtool unpack boot.img -o extracted --output-kernel /tmp/kernel.img\n";

static const char PackUsage[] =
    "Usage: bootimgtool pack [output file] [options]\n"
    "\n"
    "Options:\n"
    "  -i, --input [input directory]\n"
    "                  Input directory (current directory if unspecified)\n"
    "  -p, --prefix [prefix]\n"
    "                  Prefix to prepend to item filenames\n"
    "  -n, --noprefix  Do not prepend a prefix to the item filenames\n"
    "  -t, --type [type]\n"
    "                  Output type of the boot image\n"
    "                  [android, bump, loki, sonyelf]\n"
    "  --input-[item] [item path]\n"
    "                  Custom path for a particular item\n"
    "  --value-[item] [item value]\n"
    "                  Specify a value for an item directly\n"
    "\n"
    "The following items are loaded to create a new boot image.\n"
    "\n"
    "  cmdline *        Kernel command line                           [ABLS]\n"
    "  board *          Board name field in the header                [ABL ]\n"
    "  base *           Base address for offsets                      [ABL ]\n"
    "  kernel_offset *  Address offset of the kernel image            [ABLS]\n"
    "  ramdisk_offset * Address offset of the ramdisk image           [ABLS]\n"
    "  second_offset *  Address offset of the second bootloader image [ABL ]\n"
    "  tags_offset *    Address offset of the kernel tags image       [ABL ]\n"
    "  ipl_address *    Address of the ipl image                      [   S]\n"
    "  rpm_address *    Address of the rpm image                      [   S]\n"
    "  appsbl_address * Address of the appsbl image                   [   S]\n"
    "  entrypoint *     Address of the entrypoint                     [   S]\n"
    "  page_size *      Page size                                     [ABL ]\n"
    "  kernel`          Kernel image                                  [ABLS]\n"
    "  ramdisk          Ramdisk image                                 [ABLS]\n"
    "  second           Second bootloader image                       [ABL ]\n"
    "  dt               Device tree image                             [ABL ]\n"
    "  aboot            Aboot image                                   [  L ]\n"
    "  ipl              Ipl image                                     [   S]\n"
    "  rpm              Rpm image                                     [   S]\n"
    "  appsbl           Appsbl image                                  [   S]\n"
    "  sin              Sin image                                     [   S]\n"
    "  sinhdr           Sin header                                    [   S]\n"
    "\n"
    "Legend:\n"
    "  [A B L S]\n"
    "   | | | `- Used by Sony ELF boot images\n"
    "   | | `- Used by Loki'd Android boot images\n"
    "   | `- Used by bump'd Android boot images\n"
    "   `- Used by plain Android boot images\n"
    "\n"
    "Items marked with an asterisk can be specified by value using the --value-*\n"
    "options. (eg. --value-page_size=2048).\n"
    "\n"
    "Input files:\n"
    "\n"
    "By default, the items are loaded from the following path:\n"
    "\n"
    "    [input directory]/[prefix]-[item]\n"
    "\n"
    "If a prefix wasn't specified, then the output filename is used as a prefix.\n"
    "(eg. \"bootimgtool pack boot.img -i /tmp\" will load /tmp/boot.img-cmdline,\n"
    "..., etc.). If the -n/--noprefix option was specified, then the items are\n"
    "loaded from the following path:\n"
    "\n"
    "    [input directory]/[item]\n"
    "\n"
    "If the --input-[item]=[item path] option is specified, then that particular\n"
    "item is loaded from the specified [item path].\n"
    "\n"
    "Examples:\n"
    "\n"
    "1. To rebuild a boot image that was extracted using the bootimgtool \"unpack\"\n"
    "   command, just specify the same directory that was used to extract the boot\n"
    "   image.\n"
    "\n"
    "        bootimgtool unpack -o extracted boot.img\n"
    "        bootimgtool pack boot.img -i extracted\n"
    "\n"
    "2. Create boot.img from unpacked files in /tmp/android, but use the kernel\n"
    "   located at the path /tmp/newkernel.\n"
    "\n"
    "        bootimgtool pack boot.img -i /tmp/android --input-kernel /tmp/newkernel\n";


static std::string error_to_string(const mbp::PatcherError &error) {
    switch (error.errorCode()) {
    case mbp::ErrorCode::FileOpenError:
        return "Failed to open file: " + error.filename();
    case mbp::ErrorCode::FileReadError:
        return "Failed to read from file: " + error.filename();
    case mbp::ErrorCode::FileWriteError:
        return "Failed to write to file: " + error.filename();
    case mbp::ErrorCode::DirectoryNotExistError:
        return "Directory does not exist: " + error.filename();
    case mbp::ErrorCode::BootImageParseError:
        return "Failed to parse boot image";
    case mbp::ErrorCode::BootImageApplyBumpError:
        return "Failed to apply Bump to boot image";
    case mbp::ErrorCode::BootImageApplyLokiError:
        return "Failed to apply Loki to boot image";
    case mbp::ErrorCode::CpioFileAlreadyExistsError:
        return "File already exists in cpio archive: " + error.filename();
    case mbp::ErrorCode::CpioFileNotExistError:
        return "File does not exist in cpio archive: " + error.filename();
    case mbp::ErrorCode::ArchiveReadOpenError:
        return "Failed to open archive for reading";
    case mbp::ErrorCode::ArchiveReadDataError:
        return "Failed to read archive data for file: " + error.filename();
    case mbp::ErrorCode::ArchiveReadHeaderError:
        return "Failed to read archive entry header";
    case mbp::ErrorCode::ArchiveWriteOpenError:
        return "Failed to open archive for writing";
    case mbp::ErrorCode::ArchiveWriteDataError:
        return "Failed to write archive data for file: " + error.filename();
    case mbp::ErrorCode::ArchiveWriteHeaderError:
        return "Failed to write archive header for file: " + error.filename();
    case mbp::ErrorCode::ArchiveCloseError:
        return "Failed to close archive";
    case mbp::ErrorCode::ArchiveFreeError:
        return "Failed to free archive header memory";
    case mbp::ErrorCode::NoError:
    case mbp::ErrorCode::UnknownError:
    case mbp::ErrorCode::PatcherCreateError:
    case mbp::ErrorCode::AutoPatcherCreateError:
    case mbp::ErrorCode::RamdiskPatcherCreateError:
    case mbp::ErrorCode::XmlParseFileError:
    case mbp::ErrorCode::OnlyZipSupported:
    case mbp::ErrorCode::OnlyBootImageSupported:
    case mbp::ErrorCode::PatchingCancelled:
    case mbp::ErrorCode::SystemCacheFormatLinesNotFound:
    default:
        assert(false);
    }

    return std::string();
}

static void mbp_log_cb(mbp::LogLevel prio, const std::string &msg)
{
    switch (prio) {
    case mbp::LogLevel::Debug:
    case mbp::LogLevel::Error:
    case mbp::LogLevel::Info:
    case mbp::LogLevel::Verbose:
    case mbp::LogLevel::Warning:
        printf("%s\n", msg.c_str());
        break;
    }
}

__attribute__((format(printf, 2, 3)))
static bool write_file_fmt(const std::string &path, const char *fmt, ...)
{
    file_ptr fp(fopen(path.c_str(), "wb"), fclose);
    if (!fp) {
        return false;
    }

    va_list ap;
    va_start(ap, fmt);

    int ret = vfprintf(fp.get(), fmt, ap);

    va_end(ap);

    return ret >= 0;

}

static bool write_file_data(const std::string &path,
                            const void *data, std::size_t size)
{
    file_ptr fp(fopen(path.c_str(), "wb"), fclose);
    if (!fp) {
        return false;
    }

    if (fwrite(data, 1, size, fp.get()) != size && ferror(fp.get())) {
        return false;
    }

    return true;
}

static bool write_file_data(const std::string &path,
                            const std::vector<unsigned char> &data)
{
    return write_file_data(path, data.data(), data.size());
}

static bool read_file_data(const std::string &path,
                           std::vector<unsigned char> *out)
{
    file_ptr fp(fopen(path.c_str(), "rb"), fclose);
    if (!fp) {
        return false;
    }

    fseek(fp.get(), 0, SEEK_END);
    std::size_t size = ftell(fp.get());
    rewind(fp.get());

    std::vector<unsigned char> data(size);

    auto nread = fread(data.data(), 1, size, fp.get());
    if (nread != size) {
        return false;
    }

    data.swap(*out);
    return true;
}

static bool str_to_uint32(uint32_t *out, const char *str, int base = 0)
{
    char *end;
    errno = 0;
    uint32_t num = strtoul(str, &end, base);
    if (errno == ERANGE) {
        return false;
    }
    if (*str == '\0' || *end != '\0') {
        return false;
    }
    *out = num;
    return true;
}

bool unpack_main(int argc, char *argv[])
{
    int opt;
    bool no_prefix = false;
    std::string input_file;
    std::string output_dir;
    std::string prefix;
    std::string path_cmdline;
    std::string path_board;
    std::string path_base;
    std::string path_kernel_offset;
    std::string path_ramdisk_offset;
    std::string path_second_offset;
    std::string path_tags_offset;
    std::string path_ipl_address;
    std::string path_rpm_address;
    std::string path_appsbl_address;
    std::string path_entrypoint;
    std::string path_page_size;
    std::string path_kernel;
    std::string path_ramdisk;
    std::string path_second;
    std::string path_dt;
    std::string path_ipl;
    std::string path_rpm;
    std::string path_appsbl;
    std::string path_sin;
    std::string path_sinhdr;

    // Arguments with no short options
    enum unpack_options : int
    {
        OPT_OUTPUT_CMDLINE        = 10000 + 1,
        OPT_OUTPUT_BOARD          = 10000 + 2,
        OPT_OUTPUT_BASE           = 10000 + 3,
        OPT_OUTPUT_KERNEL_OFFSET  = 10000 + 4,
        OPT_OUTPUT_RAMDISK_OFFSET = 10000 + 5,
        OPT_OUTPUT_SECOND_OFFSET  = 10000 + 6,
        OPT_OUTPUT_TAGS_OFFSET    = 10000 + 7,
        OPT_OUTPUT_IPL_ADDRESS    = 10000 + 8,
        OPT_OUTPUT_RPM_ADDRESS    = 10000 + 9,
        OPT_OUTPUT_APPSBL_ADDRESS = 10000 + 10,
        OPT_OUTPUT_ENTRYPOINT     = 10000 + 11,
        OPT_OUTPUT_PAGE_SIZE      = 10000 + 12,
        OPT_OUTPUT_KERNEL         = 10000 + 13,
        OPT_OUTPUT_RAMDISK        = 10000 + 14,
        OPT_OUTPUT_SECOND         = 10000 + 15,
        OPT_OUTPUT_DT             = 10000 + 16,
        OPT_OUTPUT_IPL            = 10000 + 17,
        OPT_OUTPUT_RPM            = 10000 + 18,
        OPT_OUTPUT_APPSBL         = 10000 + 19,
        OPT_OUTPUT_SIN            = 10000 + 20,
        OPT_OUTPUT_SINHDR         = 10000 + 21
    };

    static struct option long_options[] = {
        // Arguments with short versions
        {"output",                required_argument, 0, 'o'},
        {"prefix",                required_argument, 0, 'p'},
        {"noprefix",              required_argument, 0, 'n'},
        // Arguments without short versions
        {"output-cmdline",        required_argument, 0, OPT_OUTPUT_CMDLINE},
        {"output-board",          required_argument, 0, OPT_OUTPUT_BOARD},
        {"output-base",           required_argument, 0, OPT_OUTPUT_BASE},
        {"output-kernel_offset",  required_argument, 0, OPT_OUTPUT_KERNEL_OFFSET},
        {"output-ramdisk_offset", required_argument, 0, OPT_OUTPUT_RAMDISK_OFFSET},
        {"output-second_offset",  required_argument, 0, OPT_OUTPUT_SECOND_OFFSET},
        {"output-tags_offset",    required_argument, 0, OPT_OUTPUT_TAGS_OFFSET},
        {"output-ipl_address",    required_argument, 0, OPT_OUTPUT_IPL_ADDRESS},
        {"output-rpm_address",    required_argument, 0, OPT_OUTPUT_RPM_ADDRESS},
        {"output-appsbl_address", required_argument, 0, OPT_OUTPUT_APPSBL_ADDRESS},
        {"output-entrypoint",     required_argument, 0, OPT_OUTPUT_ENTRYPOINT},
        {"output-page_size",      required_argument, 0, OPT_OUTPUT_PAGE_SIZE},
        {"output-kernel",         required_argument, 0, OPT_OUTPUT_KERNEL},
        {"output-ramdisk",        required_argument, 0, OPT_OUTPUT_RAMDISK},
        {"output-second",         required_argument, 0, OPT_OUTPUT_SECOND},
        {"output-dt",             required_argument, 0, OPT_OUTPUT_DT},
        {"output-ipl",            required_argument, 0, OPT_OUTPUT_IPL},
        {"output-rpm",            required_argument, 0, OPT_OUTPUT_RPM},
        {"output-appsbl",         required_argument, 0, OPT_OUTPUT_APPSBL},
        {"output-sin",            required_argument, 0, OPT_OUTPUT_SIN},
        {"output-sinhdr",         required_argument, 0, OPT_OUTPUT_SINHDR},
        {0, 0, 0, 0}
    };

    int long_index = 0;

    while ((opt = getopt_long(argc, argv, "o:p:n", long_options, &long_index)) != -1) {
        switch (opt) {
        case 'o':                       output_dir = optarg;          break;
        case 'p':                       prefix = optarg;              break;
        case 'n':                       no_prefix = true;             break;
        case OPT_OUTPUT_CMDLINE:        path_cmdline = optarg;        break;
        case OPT_OUTPUT_BOARD:          path_board = optarg;          break;
        case OPT_OUTPUT_BASE:           path_base = optarg;           break;
        case OPT_OUTPUT_KERNEL_OFFSET:  path_kernel_offset = optarg;  break;
        case OPT_OUTPUT_RAMDISK_OFFSET: path_ramdisk_offset = optarg; break;
        case OPT_OUTPUT_SECOND_OFFSET:  path_second_offset = optarg;  break;
        case OPT_OUTPUT_TAGS_OFFSET:    path_tags_offset = optarg;    break;
        case OPT_OUTPUT_IPL_ADDRESS:    path_ipl_address = optarg;    break;
        case OPT_OUTPUT_RPM_ADDRESS:    path_rpm_address = optarg;    break;
        case OPT_OUTPUT_APPSBL_ADDRESS: path_appsbl_address = optarg; break;
        case OPT_OUTPUT_ENTRYPOINT:     path_entrypoint = optarg;     break;
        case OPT_OUTPUT_PAGE_SIZE:      path_page_size = optarg;      break;
        case OPT_OUTPUT_KERNEL:         path_kernel = optarg;         break;
        case OPT_OUTPUT_RAMDISK:        path_ramdisk = optarg;        break;
        case OPT_OUTPUT_SECOND:         path_second = optarg;         break;
        case OPT_OUTPUT_DT:             path_dt = optarg;             break;
        case OPT_OUTPUT_IPL:            path_ipl = optarg;            break;
        case OPT_OUTPUT_RPM:            path_rpm = optarg;            break;
        case OPT_OUTPUT_APPSBL:         path_appsbl = optarg;         break;
        case OPT_OUTPUT_SIN:            path_sin = optarg;            break;
        case OPT_OUTPUT_SINHDR:         path_sinhdr = optarg;         break;

        case 'h':
            fprintf(stdout, UnpackUsage);
            return true;

        default:
            fprintf(stderr, UnpackUsage);
            return false;
        }
    }

    // There should be one other arguments
    if (argc - optind != 1) {
        fprintf(stderr, UnpackUsage);
        return false;
    }

    input_file = argv[optind];

    if (no_prefix) {
        prefix.clear();
    } else {
        if (prefix.empty()) {
            prefix = io::baseName(input_file);
        }
        prefix += "-";
    }

    if (output_dir.empty()) {
        output_dir = ".";
    }

    if (path_cmdline.empty())
        path_cmdline = io::pathJoin({output_dir, prefix + "cmdline"});
    if (path_board.empty())
        path_board = io::pathJoin({output_dir, prefix + "board"});
    if (path_base.empty())
        path_base = io::pathJoin({output_dir, prefix + "base"});
    if (path_kernel_offset.empty())
        path_kernel_offset = io::pathJoin({output_dir, prefix + "kernel_offset"});
    if (path_ramdisk_offset.empty())
        path_ramdisk_offset = io::pathJoin({output_dir, prefix + "ramdisk_offset"});
    if (path_second_offset.empty())
        path_second_offset = io::pathJoin({output_dir, prefix + "second_offset"});
    if (path_tags_offset.empty())
        path_tags_offset = io::pathJoin({output_dir, prefix + "tags_offset"});
    if (path_ipl_address.empty())
        path_ipl_address = io::pathJoin({output_dir, prefix + "ipl_address"});
    if (path_rpm_address.empty())
        path_rpm_address = io::pathJoin({output_dir, prefix + "rpm_address"});
    if (path_appsbl_address.empty())
        path_appsbl_address = io::pathJoin({output_dir, prefix + "appsbl_address"});
    if (path_entrypoint.empty())
        path_entrypoint = io::pathJoin({output_dir, prefix + "entrypoint"});
    if (path_page_size.empty())
        path_page_size = io::pathJoin({output_dir, prefix + "page_size"});
    if (path_kernel.empty())
        path_kernel = io::pathJoin({output_dir, prefix + "kernel"});
    if (path_ramdisk.empty())
        path_ramdisk = io::pathJoin({output_dir, prefix + "ramdisk"});
    if (path_second.empty())
        path_second = io::pathJoin({output_dir, prefix + "second"});
    if (path_dt.empty())
        path_dt = io::pathJoin({output_dir, prefix + "dt"});
    if (path_ipl.empty())
        path_ipl = io::pathJoin({output_dir, prefix + "ipl"});
    if (path_rpm.empty())
        path_rpm = io::pathJoin({output_dir, prefix + "rpm"});
    if (path_appsbl.empty())
        path_appsbl = io::pathJoin({output_dir, prefix + "appsbl"});
    if (path_sin.empty())
        path_sin = io::pathJoin({output_dir, prefix + "sin"});
    if (path_sinhdr.empty())
        path_sinhdr = io::pathJoin({output_dir, prefix + "sinhdr"});

    printf("Output files:\n");
    printf("- cmdline:        %s\n", path_cmdline.c_str());
    printf("- board:          %s\n", path_board.c_str());
    printf("- base:           %s\n", path_base.c_str());
    printf("- kernel_offset:  %s\n", path_kernel_offset.c_str());
    printf("- ramdisk_offset: %s\n", path_ramdisk_offset.c_str());
    printf("- second_offset:  %s\n", path_second_offset.c_str());
    printf("- tags_offset:    %s\n", path_tags_offset.c_str());
    printf("- ipl_address:    %s\n", path_ipl_address.c_str());
    printf("- rpm_address:    %s\n", path_rpm_address.c_str());
    printf("- appsbl_address: %s\n", path_appsbl_address.c_str());
    printf("- entrypoint:     %s\n", path_entrypoint.c_str());
    printf("- page_size:      %s\n", path_page_size.c_str());
    printf("- kernel:         %s\n", path_kernel.c_str());
    printf("- ramdisk:        %s\n", path_ramdisk.c_str());
    printf("- second:         %s\n", path_second.c_str());
    printf("- dt:             %s\n", path_dt.c_str());
    printf("- ipl:            %s\n", path_ipl.c_str());
    printf("- rpm:            %s\n", path_rpm.c_str());
    printf("- appsbl:         %s\n", path_appsbl.c_str());
    printf("- sin:            %s\n", path_sin.c_str());
    printf("- sinhdr:         %s\n", path_sinhdr.c_str());
    printf("\n");

    if (!io::createDirectories(output_dir)) {
        fprintf(stderr, "%s: Failed to create directory: %s\n",
                output_dir.c_str(), io::lastErrorString().c_str());
        return false;
    }

    // Load the boot image
    mbp::BootImage bi;
    if (!bi.loadFile(input_file)) {
        fprintf(stderr, "%s\n", error_to_string(bi.error()).c_str());
        return false;
    }

    /* Extract all the stuff! */

    // Use base relative to the default kernel offset
    uint32_t base = bi.kernelAddress() - mbp::BootImage::DefaultKernelOffset;
    uint32_t kernel_offset = mbp::BootImage::DefaultKernelOffset;
    uint32_t ramdisk_offset = bi.ramdiskAddress() - base;
    uint32_t second_offset = bi.secondBootloaderAddress() - base;
    uint32_t tags_offset = bi.kernelTagsAddress() - base;

    // Write kernel command line
    if (!write_file_fmt(path_cmdline, "%s\n", bi.kernelCmdline().c_str())) {
        fprintf(stderr, "%s: %s\n", path_cmdline.c_str(), strerror(errno));
        return false;
    }

    // Write board name field
    if (!write_file_fmt(path_board, "%s\n", bi.boardName().c_str())) {
        fprintf(stderr, "%s: %s\n", path_board.c_str(), strerror(errno));
        return false;
    }

    // Write base address on which the offsets are applied
    if (!write_file_fmt(path_base, "%08x\n", base)) {
        fprintf(stderr, "%s: %s\n", path_base.c_str(), strerror(errno));
        return false;
    }

    // Write kernel offset
    if (!write_file_fmt(path_kernel_offset, "%08x\n", kernel_offset)) {
        fprintf(stderr, "%s: %s\n", path_kernel_offset.c_str(), strerror(errno));
        return false;
    }

    // Write ramdisk offset
    if (!write_file_fmt(path_ramdisk_offset, "%08x\n", ramdisk_offset)) {
        fprintf(stderr, "%s: %s\n", path_ramdisk_offset.c_str(), strerror(errno));
        return false;
    }

    // Write second bootloader offset
    if (!write_file_fmt(path_second_offset, "%08x\n", second_offset)) {
        fprintf(stderr, "%s: %s\n", path_second_offset.c_str(), strerror(errno));
        return false;
    }

    // Write kernel tags offset
    if (!write_file_fmt(path_tags_offset, "%08x\n", tags_offset)) {
        fprintf(stderr, "%s: %s\n", path_tags_offset.c_str(), strerror(errno));
        return false;
    }

    // Write ipl address
    if (!write_file_fmt(path_ipl_address, "%08x\n", bi.iplAddress())) {
        fprintf(stderr, "%s: %s\n", path_ipl_address.c_str(), strerror(errno));
        return false;
    }

    // Write rpm address
    if (!write_file_fmt(path_rpm_address, "%08x\n", bi.rpmAddress())) {
        fprintf(stderr, "%s: %s\n", path_rpm_address.c_str(), strerror(errno));
        return false;
    }

    // Write appsbl address
    if (!write_file_fmt(path_appsbl_address, "%08x\n", bi.appsblAddress())) {
        fprintf(stderr, "%s: %s\n", path_appsbl_address.c_str(), strerror(errno));
        return false;
    }

    // Write entrypoint address
    if (!write_file_fmt(path_entrypoint, "%08x\n", bi.entrypointAddress())) {
        fprintf(stderr, "%s: %s\n", path_entrypoint.c_str(), strerror(errno));
        return false;
    }

    // Write page size
    if (!write_file_fmt(path_page_size, "%u\n", bi.pageSize())) {
        fprintf(stderr, "%s: %s\n", path_page_size.c_str(), strerror(errno));
        return false;
    }

    // Write kernel image
    if (!write_file_data(path_kernel, bi.kernelImage())) {
        fprintf(stderr, "%s: %s\n", path_kernel.c_str(), strerror(errno));
        return false;
    }

    // Write ramdisk image
    if (!write_file_data(path_ramdisk, bi.ramdiskImage())) {
        fprintf(stderr, "%s: %s\n", path_ramdisk.c_str(), strerror(errno));
        return false;
    }

    // Write second bootloader image
    if (!write_file_data(path_second, bi.secondBootloaderImage())) {
        fprintf(stderr, "%s: %s\n", path_second.c_str(), strerror(errno));
        return false;
    }

    // Write device tree image
    if (!write_file_data(path_dt, bi.deviceTreeImage())) {
        fprintf(stderr, "%s: %s\n", path_dt.c_str(), strerror(errno));
        return false;
    }

    // Write ipl image
    if (!write_file_data(path_ipl, bi.iplImage())) {
        fprintf(stderr, "%s: %s\n", path_ipl.c_str(), strerror(errno));
        return false;
    }

    // Write rpm image
    if (!write_file_data(path_rpm, bi.rpmImage())) {
        fprintf(stderr, "%s: %s\n", path_rpm.c_str(), strerror(errno));
        return false;
    }

    // Write appsbl image
    if (!write_file_data(path_appsbl, bi.appsblImage())) {
        fprintf(stderr, "%s: %s\n", path_appsbl.c_str(), strerror(errno));
        return false;
    }

    // Write sin image
    if (!write_file_data(path_sin, bi.sinImage())) {
        fprintf(stderr, "%s: %s\n", path_sin.c_str(), strerror(errno));
        return false;
    }

    // Write sinhdr image
    if (!write_file_data(path_sinhdr, bi.sinHeader())) {
        fprintf(stderr, "%s: %s\n", path_sinhdr.c_str(), strerror(errno));
        return false;
    }

    printf("\nDone\n");

    return true;
}

bool pack_main(int argc, char *argv[])
{
    int opt;
    bool no_prefix = false;
    std::string output_file;
    std::string input_dir;
    std::string prefix;
    std::string path_cmdline;
    std::string path_board;
    std::string path_base;
    std::string path_kernel_offset;
    std::string path_ramdisk_offset;
    std::string path_second_offset;
    std::string path_tags_offset;
    std::string path_ipl_address;
    std::string path_rpm_address;
    std::string path_appsbl_address;
    std::string path_entrypoint;
    std::string path_page_size;
    std::string path_kernel;
    std::string path_ramdisk;
    std::string path_second;
    std::string path_dt;
    std::string path_aboot;
    std::string path_ipl;
    std::string path_rpm;
    std::string path_appsbl;
    std::string path_sin;
    std::string path_sinhdr;
    // Values
    std::unordered_map<int, bool> values;
    std::string cmdline;
    std::string board;
    uint32_t base;
    uint32_t kernel_offset;
    uint32_t ramdisk_offset;
    uint32_t second_offset;
    uint32_t tags_offset;
    uint32_t ipl_address;
    uint32_t rpm_address;
    uint32_t appsbl_address;
    uint32_t entrypoint;
    uint32_t page_size;
    std::vector<unsigned char> kernel_image;
    std::vector<unsigned char> ramdisk_image;
    std::vector<unsigned char> second_image;
    std::vector<unsigned char> dt_image;
    std::vector<unsigned char> aboot_image;
    std::vector<unsigned char> ipl_image;
    std::vector<unsigned char> rpm_image;
    std::vector<unsigned char> appsbl_image;
    std::vector<unsigned char> sin_image;
    std::vector<unsigned char> sin_header;
    mbp::BootImage::Type type = mbp::BootImage::Type::Android;

    // Arguments with no short options
    enum pack_options : int
    {
        // Paths
        OPT_INPUT_CMDLINE        = 10000 + 1,
        OPT_INPUT_BOARD          = 10000 + 2,
        OPT_INPUT_BASE           = 10000 + 3,
        OPT_INPUT_KERNEL_OFFSET  = 10000 + 4,
        OPT_INPUT_RAMDISK_OFFSET = 10000 + 5,
        OPT_INPUT_SECOND_OFFSET  = 10000 + 6,
        OPT_INPUT_TAGS_OFFSET    = 10000 + 7,
        OPT_INPUT_IPL_ADDRESS    = 10000 + 8,
        OPT_INPUT_RPM_ADDRESS    = 10000 + 9,
        OPT_INPUT_APPSBL_ADDRESS = 10000 + 10,
        OPT_INPUT_ENTRYPOINT     = 10000 + 11,
        OPT_INPUT_PAGE_SIZE      = 10000 + 12,
        OPT_INPUT_KERNEL         = 10000 + 13,
        OPT_INPUT_RAMDISK        = 10000 + 14,
        OPT_INPUT_SECOND         = 10000 + 15,
        OPT_INPUT_DT             = 10000 + 16,
        OPT_INPUT_ABOOT          = 10000 + 17,
        OPT_INPUT_IPL            = 10000 + 18,
        OPT_INPUT_RPM            = 10000 + 19,
        OPT_INPUT_APPSBL         = 10000 + 20,
        OPT_INPUT_SIN            = 10000 + 21,
        OPT_INPUT_SINHDR         = 10000 + 22,
        // Values
        OPT_VALUE_CMDLINE        = 20000 + 1,
        OPT_VALUE_BOARD          = 20000 + 2,
        OPT_VALUE_BASE           = 20000 + 3,
        OPT_VALUE_KERNEL_OFFSET  = 20000 + 4,
        OPT_VALUE_RAMDISK_OFFSET = 20000 + 5,
        OPT_VALUE_SECOND_OFFSET  = 20000 + 6,
        OPT_VALUE_TAGS_OFFSET    = 20000 + 7,
        OPT_VALUE_IPL_ADDRESS    = 20000 + 8,
        OPT_VALUE_RPM_ADDRESS    = 20000 + 9,
        OPT_VALUE_APPSBL_ADDRESS = 20000 + 10,
        OPT_VALUE_ENTRYPOINT     = 20000 + 11,
        OPT_VALUE_PAGE_SIZE      = 20000 + 12
    };

    static struct option long_options[] = {
        // Arguments with short versions
        {"input",                required_argument, 0, 'i'},
        {"prefix",               required_argument, 0, 'p'},
        {"noprefix",             required_argument, 0, 'n'},
        {"type",                 required_argument, 0, 't'},
        // Arguments without short versions
        {"input-cmdline",        required_argument, 0, OPT_INPUT_CMDLINE},
        {"input-board",          required_argument, 0, OPT_INPUT_BOARD},
        {"input-base",           required_argument, 0, OPT_INPUT_BASE},
        {"input-kernel_offset",  required_argument, 0, OPT_INPUT_KERNEL_OFFSET},
        {"input-ramdisk_offset", required_argument, 0, OPT_INPUT_RAMDISK_OFFSET},
        {"input-second_offset",  required_argument, 0, OPT_INPUT_SECOND_OFFSET},
        {"input-tags_offset",    required_argument, 0, OPT_INPUT_TAGS_OFFSET},
        {"input-ipl_address",    required_argument, 0, OPT_INPUT_IPL_ADDRESS},
        {"input-rpm_address",    required_argument, 0, OPT_INPUT_RPM_ADDRESS},
        {"input-appsbl_address", required_argument, 0, OPT_INPUT_APPSBL_ADDRESS},
        {"input-entrypoint",     required_argument, 0, OPT_INPUT_ENTRYPOINT},
        {"input-page_size",      required_argument, 0, OPT_INPUT_PAGE_SIZE},
        {"input-kernel",         required_argument, 0, OPT_INPUT_KERNEL},
        {"input-ramdisk",        required_argument, 0, OPT_INPUT_RAMDISK},
        {"input-second",         required_argument, 0, OPT_INPUT_SECOND},
        {"input-dt",             required_argument, 0, OPT_INPUT_DT},
        {"input-aboot",          required_argument, 0, OPT_INPUT_ABOOT},
        {"input-ipl",            required_argument, 0, OPT_INPUT_IPL},
        {"input-rpm",            required_argument, 0, OPT_INPUT_RPM},
        {"input-appsbl",         required_argument, 0, OPT_INPUT_APPSBL},
        {"input-sin",            required_argument, 0, OPT_INPUT_SIN},
        {"input-sinhdr",         required_argument, 0, OPT_INPUT_SINHDR},
        // Value arguments
        {"value-cmdline",        required_argument, 0, OPT_VALUE_CMDLINE},
        {"value-board",          required_argument, 0, OPT_VALUE_BOARD},
        {"value-base",           required_argument, 0, OPT_VALUE_BASE},
        {"value-kernel_offset",  required_argument, 0, OPT_VALUE_KERNEL_OFFSET},
        {"value-ramdisk_offset", required_argument, 0, OPT_VALUE_RAMDISK_OFFSET},
        {"value-second_offset",  required_argument, 0, OPT_VALUE_SECOND_OFFSET},
        {"value-tags_offset",    required_argument, 0, OPT_VALUE_TAGS_OFFSET},
        {"value-ipl_address",    required_argument, 0, OPT_VALUE_IPL_ADDRESS},
        {"value-rpm_address",    required_argument, 0, OPT_VALUE_RPM_ADDRESS},
        {"value-appsbl_address", required_argument, 0, OPT_VALUE_APPSBL_ADDRESS},
        {"value-entrypoint",     required_argument, 0, OPT_VALUE_ENTRYPOINT},
        {"value-page_size",      required_argument, 0, OPT_VALUE_PAGE_SIZE},
        {0, 0, 0, 0}
    };

    int long_index = 0;

    while ((opt = getopt_long(argc, argv, "i:p:nt:", long_options, &long_index)) != -1) {
        switch (opt) {
        case 'i':                      input_dir = optarg;           break;
        case 'p':                      prefix = optarg;              break;
        case 'n':                      no_prefix = true;             break;
        case OPT_INPUT_CMDLINE:        path_cmdline = optarg;        break;
        case OPT_INPUT_BOARD:          path_board = optarg;          break;
        case OPT_INPUT_BASE:           path_base = optarg;           break;
        case OPT_INPUT_KERNEL_OFFSET:  path_kernel_offset = optarg;  break;
        case OPT_INPUT_RAMDISK_OFFSET: path_ramdisk_offset = optarg; break;
        case OPT_INPUT_SECOND_OFFSET:  path_second_offset = optarg;  break;
        case OPT_INPUT_TAGS_OFFSET:    path_tags_offset = optarg;    break;
        case OPT_INPUT_IPL_ADDRESS:    path_ipl_address = optarg;    break;
        case OPT_INPUT_RPM_ADDRESS:    path_rpm_address = optarg;    break;
        case OPT_INPUT_APPSBL_ADDRESS: path_appsbl_address = optarg; break;
        case OPT_INPUT_ENTRYPOINT:     path_entrypoint = optarg;     break;
        case OPT_INPUT_PAGE_SIZE:      path_page_size = optarg;      break;
        case OPT_INPUT_KERNEL:         path_kernel = optarg;         break;
        case OPT_INPUT_RAMDISK:        path_ramdisk = optarg;        break;
        case OPT_INPUT_SECOND:         path_second = optarg;         break;
        case OPT_INPUT_DT:             path_dt = optarg;             break;
        case OPT_INPUT_ABOOT:          path_aboot = optarg;          break;
        case OPT_INPUT_IPL:            path_ipl = optarg;            break;
        case OPT_INPUT_RPM:            path_rpm = optarg;            break;
        case OPT_INPUT_APPSBL:         path_appsbl = optarg;         break;
        case OPT_INPUT_SIN:            path_sin = optarg;            break;
        case OPT_INPUT_SINHDR:         path_sinhdr = optarg;         break;

        case OPT_VALUE_CMDLINE:
            path_cmdline.clear();
            values[opt] = true;
            cmdline = optarg;
            break;

        case OPT_VALUE_BOARD:
            path_board.clear();
            values[opt] = true;
            board = optarg;
            break;

        case OPT_VALUE_BASE:
            path_base.clear();
            values[opt] = true;
            if (!str_to_uint32(&base, optarg, 16)) {
                fprintf(stderr, "Invalid base: %s\n", optarg);
                return false;
            }
            break;

        case OPT_VALUE_KERNEL_OFFSET:
            path_kernel_offset.clear();
            values[opt] = true;
            if (!str_to_uint32(&kernel_offset, optarg, 16)) {
                fprintf(stderr, "Invalid kernel_offset: %s\n", optarg);
                return false;
            }
            break;

        case OPT_VALUE_RAMDISK_OFFSET:
            path_ramdisk_offset.clear();
            values[opt] = true;
            if (!str_to_uint32(&ramdisk_offset, optarg, 16)) {
                fprintf(stderr, "Invalid ramdisk_offset: %s\n", optarg);
                return false;
            }
            break;

        case OPT_VALUE_SECOND_OFFSET:
            path_second_offset.clear();
            values[opt] = true;
            if (!str_to_uint32(&second_offset, optarg, 16)) {
                fprintf(stderr, "Invalid second_offset: %s\n", optarg);
                return false;
            }
            break;

        case OPT_VALUE_TAGS_OFFSET:
            path_tags_offset.clear();
            values[opt] = true;
            if (!str_to_uint32(&tags_offset, optarg, 16)) {
                fprintf(stderr, "Invalid tags_offset: %s\n", optarg);
                return false;
            }
            break;

        case OPT_VALUE_IPL_ADDRESS:
            path_ipl_address.clear();
            values[opt] = true;
            if (!str_to_uint32(&ipl_address, optarg, 16)) {
                fprintf(stderr, "Invalid ipl_address: %s\n", optarg);
                return false;
            }
            break;

        case OPT_VALUE_RPM_ADDRESS:
            path_rpm_address.clear();
            values[opt] = true;
            if (!str_to_uint32(&rpm_address, optarg, 16)) {
                fprintf(stderr, "Invalid rpm_address: %s\n", optarg);
                return false;
            }
            break;

        case OPT_VALUE_APPSBL_ADDRESS:
            path_appsbl_address.clear();
            values[opt] = true;
            if (!str_to_uint32(&appsbl_address, optarg, 16)) {
                fprintf(stderr, "Invalid appsbl_address: %s\n", optarg);
                return false;
            }
            break;

        case OPT_VALUE_ENTRYPOINT:
            path_entrypoint.clear();
            values[opt] = true;
            if (!str_to_uint32(&entrypoint, optarg, 16)) {
                fprintf(stderr, "Invalid entrypoint: %s\n", optarg);
                return false;
            }
            break;

        case OPT_VALUE_PAGE_SIZE:
            path_page_size.clear();
            values[opt] = true;
            if (!str_to_uint32(&page_size, optarg, 10)) {
                fprintf(stderr, "Invalid page_size: %s\n", optarg);
                return false;
            }
            break;

        case 't':
            if (strcmp(optarg, "android") == 0) {
                type = mbp::BootImage::Type::Android;
            } else if (strcmp(optarg, "bump") == 0) {
                type = mbp::BootImage::Type::Bump;
            } else if (strcmp(optarg, "loki") == 0) {
                type = mbp::BootImage::Type::Loki;
            } else if (strcmp(optarg, "sonyelf") == 0) {
                type = mbp::BootImage::Type::SonyElf;
            } else {
                fprintf(stderr, "Invalid type: %s\n", optarg);
                return false;
            }
            break;

        case 'h':
            fprintf(stdout, PackUsage);
            return true;

        default:
            fprintf(stderr, PackUsage);
            return false;
        }
    }

    if (type == mbp::BootImage::Type::Loki && path_aboot.empty()) {
        fprintf(stderr, "An aboot image must be specified to create a loki image\n");
        return false;
    }

    // There should be one other argument
    if (argc - optind != 1) {
        fprintf(stderr, PackUsage);
        return false;
    }

    output_file = argv[optind];

    if (no_prefix) {
        prefix.clear();
    } else {
        if (prefix.empty()) {
            prefix = io::baseName(output_file);
        }
        prefix += "-";
    }

    if (input_dir.empty()) {
        input_dir = ".";
    }

    if (path_cmdline.empty() && !values[OPT_VALUE_CMDLINE])
        path_cmdline = io::pathJoin({input_dir, prefix + "cmdline"});
    if (path_board.empty() && !values[OPT_VALUE_BOARD])
        path_board = io::pathJoin({input_dir, prefix + "board"});
    if (path_base.empty() && !values[OPT_VALUE_BASE])
        path_base = io::pathJoin({input_dir, prefix + "base"});
    if (path_kernel_offset.empty() && !values[OPT_VALUE_KERNEL_OFFSET])
        path_kernel_offset = io::pathJoin({input_dir, prefix + "kernel_offset"});
    if (path_ramdisk_offset.empty() && !values[OPT_VALUE_RAMDISK_OFFSET])
        path_ramdisk_offset = io::pathJoin({input_dir, prefix + "ramdisk_offset"});
    if (path_second_offset.empty() && !values[OPT_VALUE_SECOND_OFFSET])
        path_second_offset = io::pathJoin({input_dir, prefix + "second_offset"});
    if (path_tags_offset.empty() && !values[OPT_VALUE_TAGS_OFFSET])
        path_tags_offset = io::pathJoin({input_dir, prefix + "tags_offset"});
    if (path_ipl_address.empty() && !values[OPT_VALUE_IPL_ADDRESS])
        path_ipl_address = io::pathJoin({input_dir, prefix + "ipl_address"});
    if (path_rpm_address.empty() && !values[OPT_VALUE_RPM_ADDRESS])
        path_rpm_address = io::pathJoin({input_dir, prefix + "rpm_address"});
    if (path_appsbl_address.empty() && !values[OPT_VALUE_APPSBL_ADDRESS])
        path_appsbl_address = io::pathJoin({input_dir, prefix + "appsbl_address"});
    if (path_entrypoint.empty() && !values[OPT_VALUE_ENTRYPOINT])
        path_entrypoint = io::pathJoin({input_dir, prefix + "entrypoint"});
    if (path_page_size.empty() && !values[OPT_VALUE_PAGE_SIZE])
        path_page_size = io::pathJoin({input_dir, prefix + "page_size"});
    if (path_kernel.empty())
        path_kernel = io::pathJoin({input_dir, prefix + "kernel"});
    if (path_ramdisk.empty())
        path_ramdisk = io::pathJoin({input_dir, prefix + "ramdisk"});
    if (path_second.empty())
        path_second = io::pathJoin({input_dir, prefix + "second"});
    if (path_dt.empty())
        path_dt = io::pathJoin({input_dir, prefix + "dt"});
    if (path_ipl.empty())
        path_ipl = io::pathJoin({input_dir, prefix + "ipl"});
    if (path_rpm.empty())
        path_rpm = io::pathJoin({input_dir, prefix + "rpm"});
    if (path_appsbl.empty())
        path_appsbl = io::pathJoin({input_dir, prefix + "appsbl"});
    if (path_sin.empty())
        path_sin = io::pathJoin({input_dir, prefix + "sin"});
    if (path_sinhdr.empty())
        path_sinhdr = io::pathJoin({input_dir, prefix + "sinhdr"});

    printf("Input files:\n");
    if (!path_cmdline.empty())
        printf("- cmdline:        (path)  %s\n", path_cmdline.c_str());
    else
        printf("- cmdline:        (value) %s\n", cmdline.c_str());
    if (!path_board.empty())
        printf("- board:          (path)  %s\n", path_board.c_str());
    else
        printf("- board:          (value) %s\n", board.c_str());
    if (!path_base.empty())
        printf("- base:           (path)  %s\n", path_base.c_str());
    else
        printf("- base:           (value) %08x\n", base);
    if (!path_kernel_offset.empty())
        printf("- kernel_offset:  (path)  %s\n", path_kernel_offset.c_str());
    else
        printf("- kernel_offset:  (value) %08x\n", kernel_offset);
    if (!path_ramdisk_offset.empty())
        printf("- ramdisk_offset: (path)  %s\n", path_ramdisk_offset.c_str());
    else
        printf("- ramdisk_offset: (value) %08x\n", ramdisk_offset);
    if (!path_second_offset.empty())
        printf("- second_offset:  (path)  %s\n", path_second_offset.c_str());
    else
        printf("- second_offset:  (value) %08x\n", second_offset);
    if (!path_tags_offset.empty())
        printf("- tags_offset:    (path)  %s\n", path_tags_offset.c_str());
    else
        printf("- tags_offset:    (value) %08x\n", tags_offset);
    if (!path_ipl_address.empty())
        printf("- ipl_address:    (path)  %s\n", path_ipl_address.c_str());
    else
        printf("- ipl_address:    (value) %08x\n", ipl_address);
    if (!path_rpm_address.empty())
        printf("- rpm_address:    (path)  %s\n", path_rpm_address.c_str());
    else
        printf("- rpm_address:    (value) %08x\n", rpm_address);
    if (!path_appsbl_address.empty())
        printf("- appsbl_address: (path)  %s\n", path_appsbl_address.c_str());
    else
        printf("- appsbl_address: (value) %08x\n", appsbl_address);
    if (!path_entrypoint.empty())
        printf("- entrypoint:     (path)  %s\n", path_entrypoint.c_str());
    else
        printf("- entrypoint:     (value) %08x\n", entrypoint);
    if (!path_page_size.empty())
        printf("- page_size:      (path)  %s\n", path_page_size.c_str());
    else
        printf("- page_size:      (value) %u\n", page_size);
    printf("- kernel:         (path)  %s\n", path_kernel.c_str());
    printf("- ramdisk:        (path)  %s\n", path_ramdisk.c_str());
    printf("- second:         (path)  %s\n", path_second.c_str());
    printf("- dt:             (path)  %s\n", path_dt.c_str());
    printf("- ipl:            (path)  %s\n", path_ipl.c_str());
    printf("- rpm:            (path)  %s\n", path_rpm.c_str());
    printf("- appsbl:         (path)  %s\n", path_appsbl.c_str());
    printf("- sin:            (path)  %s\n", path_sin.c_str());
    printf("- sinhdr:         (path)  %s\n", path_sinhdr.c_str());
    printf("\n");

    // Create new boot image
    mbp::BootImage bi;

    /* Load all the stuff! */

    // Read kernel command line
    if (!values[OPT_VALUE_CMDLINE]) {
        file_ptr fp(fopen(path_cmdline.c_str(), "rb"), fclose);
        if (fp) {
            std::vector<char> buf(mbp::BootImage::BootArgsSize);
            if (!fgets(buf.data(), mbp::BootImage::BootArgsSize, fp.get())) {
                if (ferror(fp.get())) {
                    fprintf(stderr, "%s: %s\n",
                            path_cmdline.c_str(), strerror(errno));
                    return false;
                }
            }
            cmdline = buf.data();
            auto pos = cmdline.find('\n');
            if (pos != std::string::npos) {
                cmdline.erase(pos);
            }
        } else {
            cmdline = mbp::BootImage::DefaultCmdline;
        }
    }

    // Read board name
    if (!values[OPT_VALUE_BOARD]) {
        file_ptr fp(fopen(path_board.c_str(), "rb"), fclose);
        if (fp) {
            std::vector<char> buf(mbp::BootImage::BootNameSize);
            if (!fgets(buf.data(), mbp::BootImage::BootNameSize, fp.get())) {
                if (ferror(fp.get())) {
                    fprintf(stderr, "%s: %s\n",
                            path_board.c_str(), strerror(errno));
                    return false;
                }
            }
            board = buf.data();
            auto pos = board.find('\n');
            if (pos != std::string::npos) {
                board.erase(pos);
            }
        } else {
            board = mbp::BootImage::DefaultBoard;
        }
    }

    // Read base address on which the offsets are applied
    if (!values[OPT_VALUE_BASE]) {
        file_ptr fp(fopen(path_base.c_str(), "rb"), fclose);
        if (fp) {
            int count = fscanf(fp.get(), "%08x", &base);
            if (count == EOF && ferror(fp.get())) {
                fprintf(stderr, "%s: %s\n",
                        path_base.c_str(), strerror(errno));
                return false;
            } else if (count != 1) {
                fprintf(stderr, "%s: Error: expected '%%08x' format\n",
                        path_base.c_str());
                return false;
            }
        } else {
            base = mbp::BootImage::DefaultBase;
        }
    }

    // Read kernel offset
    if (!values[OPT_VALUE_KERNEL_OFFSET]) {
        file_ptr fp(fopen(path_kernel_offset.c_str(), "rb"), fclose);
        if (fp) {
            int count = fscanf(fp.get(), "%08x", &kernel_offset);
            if (count == EOF && ferror(fp.get())) {
                fprintf(stderr, "%s: %s\n",
                        path_kernel_offset.c_str(), strerror(errno));
                return false;
            } else if (count != 1) {
                fprintf(stderr, "%s: Error: expected '%%08x' format\n",
                        path_kernel_offset.c_str());
                return false;
            }
        } else {
            kernel_offset = mbp::BootImage::DefaultKernelOffset;
        }
    }

    // Read ramdisk offset
    if (!values[OPT_VALUE_RAMDISK_OFFSET]) {
        file_ptr fp(fopen(path_ramdisk_offset.c_str(), "rb"), fclose);
        if (fp) {
            int count = fscanf(fp.get(), "%08x", &ramdisk_offset);
            if (count == EOF && ferror(fp.get())) {
                fprintf(stderr, "%s: %s\n",
                        path_ramdisk_offset.c_str(), strerror(errno));
                return false;
            } else if (count != 1) {
                fprintf(stderr, "%s: Error: expected '%%08x' format\n",
                        path_ramdisk_offset.c_str());
                return false;
            }
        } else {
            ramdisk_offset = mbp::BootImage::DefaultRamdiskOffset;
        }
    }

    // Read second bootloader offset
    if (!values[OPT_VALUE_SECOND_OFFSET]) {
        file_ptr fp(fopen(path_second_offset.c_str(), "rb"), fclose);
        if (fp) {
            int count = fscanf(fp.get(), "%08x", &second_offset);
            if (count == EOF && ferror(fp.get())) {
                fprintf(stderr, "%s: %s\n",
                        path_second_offset.c_str(), strerror(errno));
                return false;
            } else if (count != 1) {
                fprintf(stderr, "%s: Error: expected '%%08x' format\n",
                        path_second_offset.c_str());
                return false;
            }
        } else {
            second_offset = mbp::BootImage::DefaultSecondOffset;
        }
    }

    // Read kernel tags offset
    if (!values[OPT_VALUE_TAGS_OFFSET]) {
        file_ptr fp(fopen(path_tags_offset.c_str(), "rb"), fclose);
        if (fp) {
            int count = fscanf(fp.get(), "%08x", &tags_offset);
            if (count == EOF && ferror(fp.get())) {
                fprintf(stderr, "%s: %s\n",
                        path_tags_offset.c_str(), strerror(errno));
                return false;
            } else if (count != 1) {
                fprintf(stderr, "%s: Error: expected '%%08x' format\n",
                        path_tags_offset.c_str());
                return false;
            }
        } else {
            tags_offset = mbp::BootImage::DefaultTagsOffset;
        }
    }

    // Read ipl address
    if (!values[OPT_VALUE_IPL_ADDRESS]) {
        file_ptr fp(fopen(path_ipl_address.c_str(), "rb"), fclose);
        if (fp) {
            int count = fscanf(fp.get(), "%08x", &ipl_address);
            if (count == EOF && ferror(fp.get())) {
                fprintf(stderr, "%s: %s\n",
                        path_ipl_address.c_str(), strerror(errno));
                return false;
            } else if (count != 1) {
                fprintf(stderr, "%s: Error: expected '%%08x' format\n",
                        path_ipl_address.c_str());
                return false;
            }
        } else {
            ipl_address = mbp::BootImage::DefaultIplAddress;
        }
    }

    // Read rpm address
    if (!values[OPT_VALUE_RPM_ADDRESS]) {
        file_ptr fp(fopen(path_rpm_address.c_str(), "rb"), fclose);
        if (fp) {
            int count = fscanf(fp.get(), "%08x", &rpm_address);
            if (count == EOF && ferror(fp.get())) {
                fprintf(stderr, "%s: %s\n",
                        path_rpm_address.c_str(), strerror(errno));
                return false;
            } else if (count != 1) {
                fprintf(stderr, "%s: Error: expected '%%08x' format\n",
                        path_rpm_address.c_str());
                return false;
            }
        } else {
            rpm_address = mbp::BootImage::DefaultRpmAddress;
        }
    }

    // Read appsbl address
    if (!values[OPT_VALUE_APPSBL_ADDRESS]) {
        file_ptr fp(fopen(path_appsbl_address.c_str(), "rb"), fclose);
        if (fp) {
            int count = fscanf(fp.get(), "%08x", &appsbl_address);
            if (count == EOF && ferror(fp.get())) {
                fprintf(stderr, "%s: %s\n",
                        path_appsbl_address.c_str(), strerror(errno));
                return false;
            } else if (count != 1) {
                fprintf(stderr, "%s: Error: expected '%%08x' format\n",
                        path_appsbl_address.c_str());
                return false;
            }
        } else {
            appsbl_address = mbp::BootImage::DefaultAppsblAddress;
        }
    }

    // Read entrypoint address
    if (!values[OPT_VALUE_ENTRYPOINT]) {
        file_ptr fp(fopen(path_entrypoint.c_str(), "rb"), fclose);
        if (fp) {
            int count = fscanf(fp.get(), "%08x", &entrypoint);
            if (count == EOF && ferror(fp.get())) {
                fprintf(stderr, "%s: %s\n",
                        path_entrypoint.c_str(), strerror(errno));
                return false;
            } else if (count != 1) {
                fprintf(stderr, "%s: Error: expected '%%08x' format\n",
                        path_entrypoint.c_str());
                return false;
            }
        } else {
            entrypoint = mbp::BootImage::DefaultEntrypointAddress;
        }
    }

    // Read page size
    if (!values[OPT_VALUE_PAGE_SIZE]) {
        file_ptr fp(fopen(path_page_size.c_str(), "rb"), fclose);
        if (fp) {
            int count = fscanf(fp.get(), "%u", &page_size);
            if (count == EOF && ferror(fp.get())) {
                fprintf(stderr, "%s: %s\n",
                        path_page_size.c_str(), strerror(errno));
                return false;
            } else if (count != 1) {
                fprintf(stderr, "%s: Error: expected '%%u' format\n",
                        path_page_size.c_str());
                return false;
            }
        } else {
            page_size = mbp::BootImage::DefaultPageSize;
        }
    }

    // Read kernel image
    if (!read_file_data(path_kernel, &kernel_image)) {
        fprintf(stderr, "%s: %s\n", path_kernel.c_str(), strerror(errno));
        return false;
    }

    // Read ramdisk image
    if (!read_file_data(path_ramdisk, &ramdisk_image)) {
        fprintf(stderr, "%s: %s\n", path_ramdisk.c_str(), strerror(errno));
        return false;
    }

    // Read second bootloader image
    if (!read_file_data(path_second, &second_image) && errno != ENOENT) {
        fprintf(stderr, "%s: %s\n", path_second.c_str(), strerror(errno));
        return false;
    }

    // Read device tree image
    if (!read_file_data(path_dt, &dt_image) && errno != ENOENT) {
        fprintf(stderr, "%s: %s\n", path_dt.c_str(), strerror(errno));
        return false;
    }

    // Read aboot image
    if (!read_file_data(path_aboot, &aboot_image) && errno != ENOENT) {
        fprintf(stderr, "%s: %s\n", path_aboot.c_str(), strerror(errno));
        return false;
    }

    // Read ipl image
    if (!read_file_data(path_ipl, &ipl_image) && errno != ENOENT) {
        fprintf(stderr, "%s: %s\n", path_ipl.c_str(), strerror(errno));
        return false;
    }

    // Read rpm image
    if (!read_file_data(path_rpm, &rpm_image) && errno != ENOENT) {
        fprintf(stderr, "%s: %s\n", path_rpm.c_str(), strerror(errno));
        return false;
    }

    // Read appsbl image
    if (!read_file_data(path_appsbl, &appsbl_image) && errno != ENOENT) {
        fprintf(stderr, "%s: %s\n", path_appsbl.c_str(), strerror(errno));
        return false;
    }

    // Read sin image
    if (!read_file_data(path_sin, &sin_image) && errno != ENOENT) {
        fprintf(stderr, "%s: %s\n", path_sin.c_str(), strerror(errno));
        return false;
    }

    // Read sin header
    if (!read_file_data(path_sinhdr, &sin_header) && errno != ENOENT) {
        fprintf(stderr, "%s: %s\n", path_sinhdr.c_str(), strerror(errno));
        return false;
    }

    bi.setKernelCmdline(std::move(cmdline));
    bi.setBoardName(std::move(board));
    bi.setAddresses(base, kernel_offset, ramdisk_offset, second_offset, tags_offset);
    bi.setPageSize(page_size);
    bi.setKernelImage(std::move(kernel_image));
    bi.setRamdiskImage(std::move(ramdisk_image));
    bi.setSecondBootloaderImage(std::move(second_image));
    bi.setDeviceTreeImage(std::move(dt_image));
    bi.setAbootImage(std::move(aboot_image));
    bi.setIplImage(std::move(ipl_image));
    bi.setRpmImage(std::move(rpm_image));
    bi.setAppsblImage(std::move(appsbl_image));
    bi.setSinImage(std::move(sin_image));
    bi.setSinHeader(std::move(sin_header));

    bi.setTargetType(type);

    // Create boot image
    if (!bi.createFile(output_file)) {
        fprintf(stderr, "Failed to create boot image\n");
        return false;
    }

    printf("\nDone\n");

    return true;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stdout, MainUsage);
        return EXIT_FAILURE;
    }

    mbp::setLogCallback(mbp_log_cb);

    std::string command(argv[1]);
    bool ret = false;

    if (command == "unpack") {
        ret = unpack_main(--argc, ++argv);
    } else if (command == "pack") {
        ret = pack_main(--argc, ++argv);
    } else {
        fprintf(stderr, MainUsage);
        return EXIT_FAILURE;
    }

    return ret ? EXIT_SUCCESS : EXIT_FAILURE;
}
