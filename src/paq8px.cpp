/*
  PAQ8PX – Experimental Lossless Data Compressor & Entropy Estimator
  see README.md for information
  see DOC for technical details
  see CHANGELOG for version history
*/

//////////////////////// Versioning ////////////////////////////////////////

#define PROGNAME     "paq8pxhy"
#define PROGVERSION  "219"  //routed-profile v2 outer archive
#define PROGYEAR     "2026"

#define ROUTED_STRUCTURED_PROGNAME    "paq8pxrp"
#define ROUTED_STRUCTURED_PROGVERSION "218"
#define LEGACY_HYBRID_PROGNAME        "paq8pxhy"
#define LEGACY_HYBRID_PROGVERSION     "218"
#define LEGACY_STRUCTURED_PROGNAME    "paq8pxsd"
#define LEGACY_STRUCTURED_PROGVERSION "217"
#define LEGACY_ORIGINAL_PROGNAME      "paq8px"
#define LEGACY_ORIGINAL_PROGVERSION   "216"


#include "Utils.hpp"

#include <algorithm>
#include <filesystem>
#include <stdexcept>  //std::exception
#include <string> //std::stof, std::to_string
#include <system_error>
#include <vector>

#include "Encoder.hpp"
#include "ProgramChecker.hpp"
#include "Shared.hpp"
#include "String.hpp"
#include "file/FileName.hpp"
#include "file/FileTmp.hpp"
#include "file/ListOfFiles.hpp"
#include "file/fileUtils2.hpp"
#include "filter/Filters.hpp"
#include "hybrid/Backend.hpp"
#include "hybrid/RoutedArchive.hpp"
#include "hybrid/RoutedExecution.hpp"
#include "Models.hpp"
#include "Simd.hpp"
#include "PredictorBlock.hpp"
#include "PredictorMain.hpp"
#include "PredictorMainLstmOnly.hpp"


typedef enum { DoNone, DoCompress, DoExtract, DoCompare, DoList } WHATTODO;

enum class LegacyArchiveFlavor {
  ROUTED_STRUCTURED_V218,
  STRUCTURED_V217,
  ORIGINAL_V216
};

// Output creation uses "wb+" and therefore truncates immediately. Resolve
// both existing-file identity (which catches hard links and symlinks) and a
// normalized absolute spelling before any create() call can touch source data.
static std::filesystem::path normalizedAbsolutePath(const char* filename) {
  namespace fs = std::filesystem;
  if (filename == nullptr || filename[0] == 0)
    quit("Cannot validate an empty file path.");
  std::error_code error;
  fs::path absolute = fs::absolute(fs::u8path(filename), error);
  if (error) {
    error.clear();
    absolute = fs::u8path(filename);
  }
  fs::path resolved = fs::weakly_canonical(absolute, error);
  return (error ? absolute : resolved).lexically_normal();
}

static bool filesystemPathsEqual(const std::filesystem::path& left,
                                 const std::filesystem::path& right) {
#if defined(_WIN32)
  const std::wstring& leftNative = left.native();
  const std::wstring& rightNative = right.native();
  return CompareStringOrdinal(leftNative.c_str(), -1, rightNative.c_str(), -1,
                              TRUE) == CSTR_EQUAL;
#else
  return left == right;
#endif
}

static bool pathsAlias(const char* existingInput, const char* outputPath) {
  namespace fs = std::filesystem;
  std::error_code error;
  if (fs::equivalent(fs::u8path(existingInput), fs::u8path(outputPath), error))
    return true;

  const fs::path input = normalizedAbsolutePath(existingInput);
  const fs::path output = normalizedAbsolutePath(outputPath);
  return filesystemPathsEqual(input, output);
}

static void requireDistinctInputAndOutput(const char* existingInput,
                                          const char* outputPath,
                                          const char* errorMessage) {
  if (pathsAlias(existingInput, outputPath))
    quit(errorMessage);
}

static bool pathIsWithinRoot(const std::filesystem::path& root,
                             const std::filesystem::path& candidate) {
  auto rootPart = root.begin();
  auto candidatePart = candidate.begin();
  for (; rootPart != root.end(); ++rootPart, ++candidatePart) {
    if (candidatePart == candidate.end() ||
        !filesystemPathsEqual(*rootPart, *candidatePart))
      return false;
  }
  return true;
}

static bool archivedTargetsConflict(const std::filesystem::path& left,
                                    const std::filesystem::path& right) {
  if (filesystemPathsEqual(left, right))
    return true;
  return pathIsWithinRoot(left, right) || pathIsWithinRoot(right, left);
}

static bool extractionTargetsConflict(const std::filesystem::path& left,
                                      const std::filesystem::path& right) {
  namespace fs = std::filesystem;
  if (archivedTargetsConflict(left, right))
    return true;
  std::error_code error;
  if (fs::equivalent(left, right, error))
    return true;
  return false;
}

[[noreturn]] static void rejectArchivedPath(const char* pathKind,
                                            const char* archivedName,
                                            const char* reason) {
  printf("\nUnsafe archived %s '%s': %s\n", pathKind,
         archivedName == nullptr ? "" : archivedName, reason);
  quit("Refusing unsafe path from multiple-file archive.");
}

static std::filesystem::path strictOutputRoot(const char* outputRoot) {
  namespace fs = std::filesystem;
  const char* rootSpelling =
    outputRoot == nullptr || outputRoot[0] == 0 ? "." : outputRoot;
  std::error_code error;
  fs::path absolute = fs::absolute(fs::u8path(rootSpelling), error);
  if (error)
    quit("Unable to resolve the multiple-file output root.");
  fs::path resolved = fs::weakly_canonical(absolute, error);
  if (error)
    quit("Unable to canonicalize the multiple-file output root.");
  return resolved.lexically_normal();
}

static std::filesystem::path validateArchivedRelativePath(
    const char* archivedName, const char* pathKind) {
  namespace fs = std::filesystem;
  if (archivedName == nullptr || archivedName[0] == 0)
    rejectArchivedPath(pathKind, archivedName, "the filename is empty");

  std::string portableName(archivedName);
  std::replace(portableName.begin(), portableName.end(), '\\', '/');
  const bool driveSpelling = portableName.size() >= 2 &&
    ((portableName[0] >= 'A' && portableName[0] <= 'Z') ||
     (portableName[0] >= 'a' && portableName[0] <= 'z')) &&
    portableName[1] == ':';
  if (portableName[0] == '/' || driveSpelling)
    rejectArchivedPath(pathKind, archivedName,
                       "absolute, rooted, drive and UNC paths are forbidden");
  if (portableName.find("//") != std::string::npos)
    rejectArchivedPath(pathKind, archivedName,
                       "empty path components are forbidden");
#if defined(_WIN32)
  if (portableName.find(':') != std::string::npos)
    rejectArchivedPath(pathKind, archivedName,
                       "drive and alternate-data-stream paths are forbidden");
#endif
  if (portableName.back() == '/')
    rejectArchivedPath(pathKind, archivedName,
                       "the final filename component is empty");

  const fs::path relative = fs::u8path(portableName);
  if (relative.empty() || relative.is_absolute() || relative.has_root_name() ||
      relative.has_root_directory())
    rejectArchivedPath(pathKind, archivedName,
                       "the path is not a relative filename");
  for (const fs::path& part : relative) {
    if (part == fs::path(".") || part == fs::path(".."))
      rejectArchivedPath(pathKind, archivedName,
                         "'.' and '..' components are forbidden");
#if defined(_WIN32)
    std::string component = part.u8string();
    if (component.empty() || component.back() == '.' || component.back() == ' ')
      rejectArchivedPath(pathKind, archivedName,
                         "Windows path components cannot end in dot or space");
    const size_t dot = component.find('.');
    std::string device = component.substr(0, dot);
    const size_t tilde = device.rfind('~');
    bool numericTildeSuffix = tilde != std::string::npos && tilde > 0 &&
                              tilde + 1 < device.size();
    for (size_t index = numericTildeSuffix ? tilde + 1 : device.size();
         numericTildeSuffix && index < device.size(); ++index) {
      if (device[index] < '0' || device[index] > '9')
        numericTildeSuffix = false;
    }
    const bool shortExtension = dot == std::string::npos ||
                                component.size() - dot - 1 <= 3;
    if (numericTildeSuffix && tilde <= 6 && device.size() <= 8 &&
        shortExtension)
      rejectArchivedPath(pathKind, archivedName,
                         "DOS 8.3 short-name aliases are forbidden");
    while (!device.empty() &&
           (device.back() == '.' || device.back() == ' '))
      device.pop_back();
    for (char& ch : device) {
      if (ch >= 'a' && ch <= 'z')
        ch = static_cast<char>(ch - 'a' + 'A');
    }
    const bool numberedDevice = device.size() == 4 &&
      (device.compare(0, 3, "COM") == 0 ||
       device.compare(0, 3, "LPT") == 0) &&
      device[3] >= '1' && device[3] <= '9';
    const bool superscriptDevice = device.size() == 5 &&
      (device.compare(0, 3, "COM") == 0 ||
       device.compare(0, 3, "LPT") == 0) &&
      static_cast<unsigned char>(device[3]) == 0xc2 &&
      (static_cast<unsigned char>(device[4]) == 0xb9 ||
       static_cast<unsigned char>(device[4]) == 0xb2 ||
       static_cast<unsigned char>(device[4]) == 0xb3);
    if (device == "CON" || device == "PRN" || device == "AUX" ||
        device == "NUL" || device == "CLOCK$" || device == "CONIN$" ||
        device == "CONOUT$" || numberedDevice || superscriptDevice)
      rejectArchivedPath(pathKind, archivedName,
                         "Windows reserved device names are forbidden");
#endif
  }
  return relative;
}

static std::filesystem::path resolveContainedExtractionTarget(
    const std::filesystem::path& outputRoot,
    const std::filesystem::path& relative,
    const char* archivedName, const char* pathKind) {
  namespace fs = std::filesystem;
  fs::path resolved = outputRoot;
  for (auto part = relative.begin(); part != relative.end(); ++part) {
    auto following = part;
    ++following;
    const bool finalComponent = following == relative.end();
    fs::path next = (resolved / *part).lexically_normal();
    std::error_code error;
    const fs::file_status status = fs::symlink_status(next, error);
    if (error && error != std::errc::no_such_file_or_directory)
      rejectArchivedPath(pathKind, archivedName,
                         "a target path component cannot be inspected safely");
    if (!error && fs::is_symlink(status)) {
      fs::path linkTarget = fs::read_symlink(next, error);
      if (error)
        rejectArchivedPath(pathKind, archivedName,
                           "a symbolic link target cannot be resolved safely");
      if (linkTarget.is_relative())
        linkTarget = next.parent_path() / linkTarget;
      resolved = fs::weakly_canonical(linkTarget, error);
      if (error)
        rejectArchivedPath(pathKind, archivedName,
                           "a symbolic link target cannot be canonicalized");
      resolved = resolved.lexically_normal();
    }
    else {
      resolved = next;
    }
    if (!pathIsWithinRoot(outputRoot, resolved))
      rejectArchivedPath(pathKind, archivedName,
                         "the resolved target escapes the output root");
    if (!finalComponent) {
      error.clear();
      const fs::file_status resolvedStatus = fs::status(resolved, error);
      if (error && error != std::errc::no_such_file_or_directory)
        rejectArchivedPath(
          pathKind, archivedName,
          "an intermediate target component cannot be inspected safely");
      if (!error && fs::exists(resolvedStatus) &&
          !fs::is_directory(resolvedStatus))
        rejectArchivedPath(pathKind, archivedName,
                           "an intermediate target component is not a directory");
    }
  }

  std::error_code error;
  resolved = fs::weakly_canonical(resolved, error);
  if (error)
    rejectArchivedPath(pathKind, archivedName,
                       "the output target cannot be canonicalized");
  resolved = resolved.lexically_normal();
  if (!pathIsWithinRoot(outputRoot, resolved))
    rejectArchivedPath(pathKind, archivedName,
                       "the canonical target escapes the output root");
  if (filesystemPathsEqual(outputRoot, resolved))
    rejectArchivedPath(pathKind, archivedName,
                       "the target does not name a file below the output root");
  error.clear();
  const fs::file_status finalStatus = fs::status(resolved, error);
  if (error && error != std::errc::no_such_file_or_directory)
    rejectArchivedPath(pathKind, archivedName,
                       "the final target cannot be inspected safely");
  if (!error && fs::exists(finalStatus)) {
    if (!fs::is_regular_file(finalStatus))
      rejectArchivedPath(pathKind, archivedName,
                         "the final target is not a regular file");
    error.clear();
    const uintmax_t linkCount = fs::hard_link_count(resolved, error);
    if (error)
      rejectArchivedPath(pathKind, archivedName,
                         "the final target link count cannot be inspected");
    if (linkCount > 1)
      rejectArchivedPath(pathKind, archivedName,
                         "the final target has another hard-link name");
  }
  return resolved;
}

static std::string validatedExtractionTarget(
    const std::filesystem::path& outputRoot, const char* archivedName,
    const char* archiveName, const char* pathKind) {
  const std::filesystem::path relative =
    validateArchivedRelativePath(archivedName, pathKind);
  const std::filesystem::path target = resolveContainedExtractionTarget(
    outputRoot, relative, archivedName, pathKind);
  const std::string targetUtf8 = target.u8string();
  if (pathsAlias(archiveName, targetUtf8.c_str()))
    rejectArchivedPath(pathKind, archivedName,
                       "the target aliases the input archive");
  return targetUtf8;
}

static bool consumeArchiveMagic(File* archive, const char* magic) {
  archive->setpos(0);
  const size_t length = strlen(magic);
  for (size_t i = 0; i < length; ++i) {
    if (archive->getchar() != static_cast<uint8_t>(magic[i])) {
      archive->setpos(0);
      return false;
    }
  }
  return true;
}

static int readRequiredArchiveByte(File* archive, const char* errorMessage) {
  const int value = archive->getchar();
  if (value == EOF)
    quit(errorMessage);
  return value;
}

static LegacyArchiveFlavor readLegacyArchiveSettings(File* archive, int& level,
                                                      Shared& shared) {
  LegacyArchiveFlavor flavor;
  if (consumeArchiveMagic(archive, ROUTED_STRUCTURED_PROGNAME)) {
    flavor = LegacyArchiveFlavor::ROUTED_STRUCTURED_V218;
  }
  else if (consumeArchiveMagic(archive, LEGACY_STRUCTURED_PROGNAME)) {
    flavor = LegacyArchiveFlavor::STRUCTURED_V217;
  }
  else if (consumeArchiveMagic(archive, LEGACY_ORIGINAL_PROGNAME)) {
    flavor = LegacyArchiveFlavor::ORIGINAL_V216;
  }
  else {
    quit("Payload is neither a paq8pxrp v218, paq8pxsd v217 nor paq8px v216 archive.");
  }

  level = readRequiredArchiveByte(archive, "Unexpected end of legacy PAQ archive header.");
  shared.options = static_cast<uint8_t>(
      readRequiredArchiveByte(archive, "Unexpected end of legacy PAQ archive header."));
  if (level > 12)
    quit("Unexpected compression level setting in archive.");
  if (shared.GetOptionUseLSTM()) {
    const int layers = readRequiredArchiveByte(
        archive, "Unexpected end of legacy PAQ archive header.");
    if (layers < 1 || layers > 5)
      quit("Unexpected LSTM layer setting in archive.");
    shared.LstmSettings.num_layers = layers;
  }
  return flavor;
}

static const char* legacyArchiveExtension(LegacyArchiveFlavor flavor) {
  switch (flavor) {
    case LegacyArchiveFlavor::ROUTED_STRUCTURED_V218:
      return "." ROUTED_STRUCTURED_PROGNAME ROUTED_STRUCTURED_PROGVERSION;
    case LegacyArchiveFlavor::STRUCTURED_V217:
      return "." LEGACY_STRUCTURED_PROGNAME LEGACY_STRUCTURED_PROGVERSION;
    case LegacyArchiveFlavor::ORIGINAL_V216:
      return "." LEGACY_ORIGINAL_PROGNAME LEGACY_ORIGINAL_PROGVERSION;
  }
  quit("Unknown PAQ archive flavor.");
  return "";
}

static void printHelp() {
  printf(
    "Free under GPL, http://www.gnu.org/licenses/gpl.txt\n\n"
    "Usage:\n"
    "  to compress       ->   " PROGNAME " -LEVEL[FLAGS] [OPTIONS] INPUT [OUTPUT]\n"
    "  to decompress     ->   " PROGNAME " -d INPUT." PROGNAME PROGVERSION " [OUTPUT]\n"
    "  to test           ->   " PROGNAME " -t INPUT." PROGNAME PROGVERSION " [OUTPUT]\n"
    "  to list contents  ->   " PROGNAME " -l INPUT." PROGNAME PROGVERSION "\n"
    "\n"
    "LEVEL:\n"
    "  -1 -2 -3 -4          | Compress using less memory (558, 572, 602, 660 MB)\n"
    "  -5 -6 -7 -8          | Use more memory (776, 1010, 1476, 2408 MB)\n"
    "  -9 -10 -11 -12       | Use even more memory (4273, 8003, 15463, 29358 MB)\n"
    "  -0                   | Segment and transform only, no compression\n"
    "  -0L                  | Segment and transform then LSTM-only compression (alternative: -lstmonly)\n"
    "\n"
    "FLAGS:\n"
    "  L                    | Enable LSTM model (+24 MB per block type)\n"
    "  A                    | Use adaptive learning rate\n"
    "  S                    | Skip RGB color transform (images)\n"
    "  B                    | Brute-force DEFLATE detection\n"
    "  E                    | Pre-train x86/x64 model\n"
    "  T                    | Pre-train text models (dictionary-based)\n"
    "\n"
    "  Example: " PROGNAME " -8LA file.txt   <- Level 8 + LSTM + adaptive learning rate\n"
    "\n"
    "Block detection control (compression-only):\n"
    "  -forcebinary         | Force generic (binary) mode\n"
    "  -forcetext           | Force text mode\n"
    "\n"
    "LSTM-specific options (expert-only):\n"
    "  -lstmlayers=N        | Set the number of LSTM layers to N (1..5, default is 2)\n"
    "  -savelstm:text FILE  | Save learned LSTM model weights after compression\n"
    "  -loadlstm:text FILE  | Load LSTM model weights before compression/decompression\n"
    "\n"
    "Misc options:\n"
    "  -v                   | Verbose output\n"
    "  -log FILE            | Append compression results to log file\n"
    "  -simd MODE           | Override SIMD detection - expert only (NONE|SSE2|SSE3|SSE41|AVX2|AVX512|NEON)\n"
    "\n"
    "Notes:\n"
    "  INPUT may be FILE, PATH/FILE, or @FILELIST\n"
    "  OUTPUT is optional: FILE, PATH, PATH/FILE\n"
    "  The archive is created in the current folder with ." PROGNAME PROGVERSION " extension if OUTPUT omitted\n"
    "  FLAGS are case-insensitive and only needed for compression; they may appear in any order\n"
    "  INPUT must precede OUTPUT; all other OPTIONS may appear anywhere\n"
  );
}

static void printHelpVerbose() {
  printf("\n"
    "=============\n"
    "Detailed Help\n"
    "=============\n"
    "\n"
    "---------------\n"
    " 1. Compression\n"
    "---------------\n"
    "\n"
    "  Compression levels control the amount of memory used during both compression and decompression.\n"
    "  Higher levels generally improve compression ratio at the cost of higher memory usage and slower speed.\n"
    "  Specifying the compression level is needed only for compression - no need to specify it for decompression.\n"
    "  Approximately the same amount of memory will be used during compression and decompression.\n"
    "\n"
    "  The listed memory usage for each LEVEL (-1 = 558 MB .. -12 = 29358 MB) is typical/indicative for compressing binary\n"
    "  files with no preprocessing. Actual memory use is lower for text files and higher when a preprocessing step\n"
    "  (segmentation and transformations) requires temporary memory. When special file types are detected, special models\n"
    "  (image, jpg, audio) will be used and thus will require extra RAM.\n"
    "\n"
    "------------------\n"
    " 2. Special Levels\n"
    "------------------\n"
    "\n"
    "  -0   Only block type detection (segmentation) and block transformations are performed.\n"
    "       The data is copied (verbatim or transformed); no compression happens.\n"
    "       This mode is similar to a preprocessing-only tool like precomp.\n"
    "       Uses approximately 3-7 MB total.\n"
    "\n"
    "  -0L  Uses only a single LSTM model for prediction, which is shared across all block types.\n"
    "       Uses approximately 20-24 MB total RAM.\n"
    "       Alternative: -lstmonly\n"
    "\n"
    "---------------------\n"
    " 3. Compression Flags\n"
    "---------------------\n"
    "\n"
    "  Compression flags are single-letter, case-insensitive, and appended directly to the level.\n"
    "  They are valid only during compression. No need to specify them for decompression.\n"
    "\n"
    "  L   Enable the LSTM (Long Short-Term Memory) model.\n"
    "      Uses a fixed-size model, independent of compression level.\n"
    "\n"
    "      At level -0L (also: -lstmonly) a single LSTM model is used for prediction for all detected block types.\n"
    "      Block detection and segmentation are still performed, but no Context Mixing or Secondary Symbol\n"
    "      Estimation (SSE) stage is used.\n"
    "\n"
    "      At higher levels (-1L .. -12L) the LSTM model is included as a submodel in Context Mixing and its predictions\n"
    "      are mixed with the other models.\n"
    "      When special block types are detected, for each block type an individual LSTM model is created dynamically and\n"
    "      used within that block type. Each such LSTM model adds approximately 24 MB to the total memory use.\n"
    "\n"
    "  A   Enable adaptive learning rate in the CM mixer.\n"
    "      May improve compression for some files.\n"
    "\n"
    "  S   Skip RGB color transform for 24/32-bit images.\n"
    "      Useful when the transform worsens compression.\n"
    "      This flag has no effect when no image block types are detected.\n"
    "\n"
    "  B   Enable brute-force DEFLATE stream detection.\n"
    "      Slower but may improve detection of compressed streams.\n"
    "\n"
    "  E   Pre-train the x86/x64 executable model.\n"
    "      This option pre-trains the EXE model using the " PROGNAME ".exe binary itself.\n"
    "      Archives created with a different " PROGNAME ".exe executable (even when built from the same source and build options)\n"
    "      will differ. To decompress an archive created with -E, you must use the exact same executable that created it.\n"
    "\n"
    "  T   Pre-train text-oriented models using a dictionary and expression list.\n"
    "      The word list (english.dic) and expression list (english.exp) are used only to pre-train models before\n"
    "      compression and they are not stored in the archive.\n"
    "      You must have these same files available to decompress archives created with -T.\n"
    "\n"
    "---------------------------\n"
    " 4. Block Detection Control\n"
    "---------------------------\n"
    "\n"
    "  Block detection and segmentation always happen regardless of the memory level or other options - except when forced:\n"
    "\n"
    "  -forcebinary\n"
    "\n"
    "      Disable block detection; the whole file is considered as a single binary block and only the generic (binary)\n"
    "      model set will be used.\n"
    "      Useful when block detection produces false positives.\n"
    "\n"
    "  -forcetext\n"
    "\n"
    "      Disable block detection; consider the whole file as a single text block and use the text model set only.\n"
    "      Useful when text data is misclassified as binary or fragments in a text file are incorrectly detected as some\n"
    "      other block type.\n"
    "\n"
    "---------------------------------------\n"
    " 5. LSTM-Specific Options (expert-only)\n"
    "---------------------------------------\n"
    "\n"
    "  -lstmlayers=N\n"
    "\n"
    "      Set the number of LSTM layers to N. Using more layers generally leads to better compression, but memory use\n"
    "      will be higher (scales linearly with N) and compression time will be significantly slower. The default is N=2.\n"
    "\n"
    "  -savelstm:text FILE\n"
    "\n"
    "      Saves the LSTM model's learned parameters as a lossless snapshot to the specified file when compression finishes.\n"
    "      Only the model used for text block(s) will be saved.\n"
    "      It's not possible to save a snapshot from other block types. This is an experimental feature.\n"
    "\n"
    "  -loadlstm:text FILE\n"
    "\n"
    "      Loads the LSTM model's learned parameters from the specified file (which was saved earlier\n"
    "      by the -savelstm:text option) before compression starts. The LSTM model will use this loaded\n"
    "      snapshot to bootstrap its predictions.\n"
    "      At levels -1L .. -12L only text blocks are affected.\n"
    "      At level -0L all blocks are affected (because a single LSTM model is used for all block types).\n"
    "      Critical: The same snapshot file MUST be used during decompression or the original content cannot be recovered.\n"
    "\n"
    "----------------------\n"
    " 6. Archive Operations\n"
    "----------------------\n"
    "\n"
    "  -d  Decompress an archive.\n"
    "      In single-file mode the content is decompressed, the name of the output is the name of the archive without\n"
    "      the ." PROGNAME PROGVERSION " extension.\n"
    "      In multi-file mode first the @LISTFILE is extracted then the rest of the files. Any required folders will\n"
    "      be created recursively, all files will be extracted with their original names.\n"
    "      If the output file or files already exist they will be overwritten.\n"
    "\n"
    "      Example: to decompress file.txt to the current folder:\n"
    "      " PROGNAME " -d file.txt." PROGNAME PROGVERSION "\n"
    "\n"
    "  -t  Test archive contents by decompressing to memory and comparing with the original data on-the-fly.\n"
    "      If a file fails the test, the first mismatched position will be printed to screen.\n"
    "\n"
    "      Example: to test archive contents:\n"
    "      " PROGNAME " -t file.txt." PROGNAME PROGVERSION "\n"
    "\n"
    "  -l  List archive contents.\n"
    "      Extracts and prints the embedded @FILELIST (if present).\n"
    "      Applicable only to multi-file archives.\n"
    "\n"
    "      Example: to list the file list (when the archive was created using @files):\n"
    "      " PROGNAME " -l files." PROGNAME PROGVERSION "\n"
    "\n"
    "----------------------------------\n"
    " 7. INPUT and OUTPUT Specification\n"
    "----------------------------------\n"
    "\n"
    "  INPUT may be:\n"
    "\n"
    "  * A single file\n"
    "  * A path/file\n"
    "  * A [path/]@FILELIST\n"
    "\n"
    "  In multi-file mode (i.e. when @FILELIST is provided) only file names, file contents and file sizes are stored\n"
    "  in the archive. Timestamps, permissions, attributes or any other metadata are not preserved unless stored\n"
    "  separately and manually by the user in the FILELIST.\n"
    "\n"
    "  OUTPUT is optional:\n"
    "\n"
    "    For compression:\n"
    "\n"
    "    * If omitted, the archive is created in the current directory.\n"
    "      The name of the archive: INPUT + " PROGNAME PROGVERSION " extension appended.\n"
    "    * If a filename is given, it is used as the archive name.\n"
    "    * If a directory is given, the archive is created inside it.\n"
    "    * If the archive file already exists, it will be overwritten.\n"
    "\n"
    "    For decompression:\n"
    "\n"
    "    * If an output filename is not provided, the output will be named the same as the archive without\n"
    "      the " PROGNAME PROGVERSION " extension.\n"
    "    * If a filename is given, it is used as the output name.\n"
    "    * If a directory is given, the restored file will be created inside it (the directory must exist).\n"
    "    * If the output file(s) already exist, they will be overwritten.\n"
    "\n"
    "  Examples:\n"
    "\n"
    "  To create data.txt." PROGNAME PROGVERSION " in current directory:\n"
    "  " PROGNAME " -8 data.txt\n"
    "\n"
    "  To create archive." PROGNAME PROGVERSION " in current directory:\n"
    "  " PROGNAME " -8 data.txt archive." PROGNAME PROGVERSION "\n"
    "\n"
    "  To create data.txt." PROGNAME PROGVERSION " in results/ directory:\n"
    "  " PROGNAME " -8 data.txt results/\n"
    "\n"
    "---------------------------------\n"
    " 8. @FILELIST Format and Behavior\n"
    "---------------------------------\n"
    "\n"
    "  When a @FILELIST is provided, the FILELIST file itself is compressed as the first file in the archive and\n"
    "  automatically extracted during decompression.\n"
    "\n"
    "  The FILELIST is a tab-separated text file with this structure:\n"
    "\n"
    "    Column 1:  Filenames and optional relative paths (required, used by compressor)\n"
    "    Column 2+: Arbitrary metadata - timestamps, ownership, etc. (optional, preserved but ignored)\n"
    "\n"
    "    First line: Header (preserved but ignored during processing the file list)\n"
    "\n"
    "  Only the first column is used by the compressor and decompressor.\n"
    "  All other columns are preserved but ignored.\n"
    "  Paths must be relative to the FILELIST location.\n"
    "\n"
    "  Using this mechanism allows full restoration of file metadata with third-party tools after decompression.\n"
    "  \n"
    "\n"
    "-------------------------\n"
    " 9. Miscellaneous Options\n"
    "-------------------------\n"
    "\n"
    "  -v\n"
    "\n"
    "    Enable verbose output.\n"
    "\n"
    "  -log FILE\n"
    "\n"
    "    Append compression results to a tab-separated log file.\n"
    "    Logging applies only to compression.\n"
    "\n"
    "  -simd MODE\n"
    "\n"
    "    Normally, the highest usable SIMD instruction set is detected and used automatically\n"
    "\n"
    "    - for the CM mixer - supported: SSE2, AVX2, AVX512, ARM NEON\n"
    "    - for neural network operations in the LSTM model - supported: SSE2, AVX2\n"
    "    - for the LSM and OLS predictors (used mainly in image and audio models) - supported: SSE3.\n"
    "    - for the SimilarityModel: AVX2, SSE41.\n"
    "\n"
    "    This option overrides the detected SIMD instruction set. Intended for expert use and benchmarking.\n"
    "    Supported values (case-insensitive):\n"
    "       NONE\n"
    "       SSE2, SSE3, SSE41, AVX2, AVX512 (on x64)\n"
    "       NEON (on ARM)\n"
    "\n"
    "    Note that when paq8px is compiled for a specific CPU architecture, the compiler may automatically\n"
    "    vectorize some parts of the code. While selecting 'NONE' disables all manually optimized SIMD\n"
    "    implementations, the remaining scalar code may still be auto-vectorized by the compiler and\n"
    "    therefore may not be entirely free of vector instructions.\n"
    "\n"
    "----------------------\n"
    " 10. Argument Ordering\n"
    "----------------------\n"
    "\n"
    "  Command-line arguments may appear in any order with the following exception:\n"
    "  INPUT must always precede OUTPUT.\n"
    "\n"
    "  Example: the following two are equivalent:\n"
    "\n"
    "    " PROGNAME " -v -simd sse2 enwik8 -log results.txt output/ -8\n"
    "    " PROGNAME " -8 enwik8 -log results.txt output/ -v -simd sse2\n"
    "\n"
    "  Further examples:\n"
    "\n"
    "    " PROGNAME " -8 file.txt         | Compress using ~2.3 GB RAM\n"
    "    " PROGNAME " -12L enwik8         | Compress 'enwik8' with maximum compression (~29 GB RAM), use the LSTM model as well\n"
    "    " PROGNAME " -4 image.jpg        | Compress the 'image.jpg' file - using less memory, even faster\n"
    "    " PROGNAME " -8ba b64sample.xml  | Compress 'b64sample.xml' faster and using less memory\n"
    "                                 Put more effort into finding and transforming DEFLATE blocks\n"
    "                                 Use adaptive learning rate.\n"
    "    " PROGNAME " -8s rafale.bmp      | Compress the 'rafale.bmp' image file\n"
    "                                 Skip color transform - this file compresses better without it\n"
  );
}

static void printModules() {
  printf("\n");
  printf("Build: ");
#ifndef DISABLE_ZLIB
  printf("ZLIB: ENABLED, ");
#else
  printf("ZLIB: DISABLED, ");
#endif

#if defined(PAQ_ENABLE_OPENZL)
  printf("OpenZL v0.2.0 routed expert: ENABLED");
#else
  printf("OpenZL routed expert: DISABLED (SAO falls back to PAQ)");
#endif

  printf("\n");
}

static void printSimdInfo(int simdIset, int detectedSimdIset) {
  printf("\nHighest SIMD vectorization support on this system: ");
  if( detectedSimdIset < 0 || detectedSimdIset > 11 ) {
    quit("Oops, sorry. Unexpected result.");
  }
  static const char *vectorizationString[12] = {"none", "MMX", "SSE", "SSE2", "SSE3", "SSSE3", "SSE4.1", "SSE4.2", "AVX", "AVX2", "AVX512", "ARM Neon"};
  printf("%s.\n", vectorizationString[detectedSimdIset]);

  printf("Using ");
  if (simdIset == 11) {
    printf("NEON");
  } else if( simdIset >= 10 ) {
    printf("AVX512");
  } else if( simdIset >= 9 ) {
    printf("AVX2");
  } else if( simdIset >= 6 ) {
    printf("SSE3");
  } else if( simdIset >= 4 ) {
    printf("SSE3");
  } else if( simdIset >= 3 ) {
    printf("SSE2");
  } else {
    printf("non-vectorized");
  }
  printf(" neural network functions.\n");
}

static void printCommand(const WHATTODO &whattodo) {
  printf(" To do          = ");
  if( whattodo == DoNone ) {
    printf("-");
  }
  if( whattodo == DoCompress ) {
    printf("Compress");
  }
  if( whattodo == DoExtract ) {
    printf("Extract");
  }
  if( whattodo == DoCompare ) {
    printf("Compare");
  }
  if( whattodo == DoList ) {
    printf("List");
  }
  printf("\n");
}

static void printOptions(Shared *shared, int level) {
  printf(" Level          = %d\n", level);
  printf(" Brute      (b) = %s\n", shared->GetOptionBruteforceDeflateDetection() ?
    "On  (Brute-force detection of DEFLATE streams)" : 
    "Off"); //this is a compression-only option, but we put/get it for reproducibility
  printf(" Train exe  (e) = %s\n", shared->GetOptionTrainExe() ? "On  (Pre-train x86/x64 model)" : "Off");
  printf(" Train txt  (t) = %s\n", shared->GetOptionTrainTxt() ? "On  (Pre-train main model with word and expression list)" : "Off");
  printf(" Adaptive   (a) = %s\n", shared->GetOptionAdaptiveLearningRate() ? "On  (Adaptive learning rate)" : "Off");
  printf(" Skip RGB   (s) = %s\n", shared->GetOptionSkipRGB() ? "On  (Skip the color transform, just reorder the RGB channels)" : "Off");
  printf(" Use LSTM   (l) = %s\n", shared->GetOptionUseLSTM() ? "On  (Use LSTM (Long Short-Term Memory) model)" : "Off");
  printf(" File mode      = %s\n", shared->GetOptionMultipleFileMode() ? "Multiple" : "Single");
}

static void writeCompressionLog(const FileName& logfile, int argc,
                                char** argv, const Shared& shared,
                                const FileName& input, uint64_t contentSize,
                                uint64_t archiveSize, double runtimeSeconds) {
  if (logfile.strsize() == 0)
    return;
  const bool showParam = shared.tuning_param != 0.0f;
  String results;
  const int pathType = examinePath(logfile.c_str());
  if (pathType == 3 ||
      (pathType == 1 && getFileSize(logfile.c_str()) == 0)) {
    results += "PROG_NAME\tPROG_VERSION\tCOMMAND_LINE\tLEVEL\t";
    if (showParam)
      results += "PARAM\t";
    results += "INPUT_FILENAME\tORIGINAL_SIZE_BYTES\t"
               "COMPRESSED_SIZE_BYTES\tRUNTIME_MS\n";
  }
  results += PROGNAME "\t" PROGVERSION "\t";
  for (int index = 1; index < argc; ++index) {
    if (index != 1)
      results += ' ';
    results += argv[index];
  }
  results += "\t";
  results += uint64_t(shared.level);
  results += "\t";
  if (showParam) {
    results += std::to_string(shared.tuning_param).c_str();
    results += "\t";
  }
  results += input.c_str();
  results += "\t";
  results += contentSize;
  results += "\t";
  results += archiveSize;
  results += "\t";
  results += uint64_t(runtimeSeconds * 1000.0);
  results += "\t\n";
  appendToFile(logfile.c_str(), results.c_str());
  printf("Results logged to file '%s'\n\n", logfile.c_str());
}

int processCommandLine(int argc, char **argv) {
  ProgramChecker *programChecker = ProgramChecker::getInstance();
  Shared shared;
  try {

    if( !shared.toScreen ) { //we need a minimal feedback when redirected
      fprintf(stderr, PROGNAME " data compressor v" PROGVERSION " (c) " PROGYEAR ", Matt Mahoney et al.\n");
    }
    printf(PROGNAME " v" PROGVERSION " (c) " PROGYEAR ", Matt Mahoney et al.\n");

    // Print help message
    if( argc < 2 ) {
      printHelp();
      printf(
        "\n"
        "For detailed help with examples and more explanation, use:\n"
        "  " PROGNAME " -help\n"
      );
      quit();
    }

    if ( strcasecmp(argv[1], "-help") == 0 ) {
      printHelp();
      printHelpVerbose();
      quit();
    }


    // Parse command line arguments
    WHATTODO whattodo = DoNone;
    bool verbose = false;
    int simdIset = -1; //simd instruction set to use
    bool lstmOnly = false;
    int level = 0;
    
    FileName input;
    FileName output;
    FileName inputPath;
    FileName outputPath;
    FileName archiveName;
    FileName logfile;
    FileName lstmLoadFilename;
    FileName lstmSaveFilename;

    for( int i = 1; i < argc; i++ ) {
      int argLen = static_cast<int>(strlen(argv[i]));
      if( argv[i][0] == '-' ) {
        if( argLen == 1 ) {
          quit("Empty command.");
        }
        if( argv[i][1] >= '0' && argv[i][1] <= '9' ) { // first  digit of level
          if( whattodo != DoNone ) {
            quit("Only one command may be specified.");
          }
          level = argv[i][1] - '0';
          int j = 2;
          if (argLen >= 3 && argv[i][2] >= '0' && argv[i][2] <= '9') { // second digit of level
            level = level*10 + argv[i][2] - '0';
            j++;
          }
          if (level > 12) {
            quit("Compression level must be between 0 and 12.");
          }
          whattodo = DoCompress;
          //process optional compression switches
          for( ; j < argLen; j++ ) {
            switch( argv[i][j] & 0xDF ) {
              case 'B':
                shared.SetOptionBruteforceDeflateDetection();
                break;
              case 'E':
                shared.SetOptionTrainExe();
                break;
              case 'T':
                shared.SetOptionTrainTxt();
                break;
              case 'A':
                shared.SetOptionAdaptiveLearningRate();
                break;
              case 'S':
                shared.SetOptionSkipRGB();
                break;
              case 'L':
                shared.SetOptionUseLSTM();
                break;
              case 'R':
                printf("The -R option is temporarily unavailable in this release. Until it's back, you may try the -savelstm:text and -loadlstm:text options to create your own repository.");
                quit();
              break;
              default: {
                printf("Invalid compression switch: %c", argv[1][j]);
                quit();
              }
            }
          }
        } else if( strcasecmp(argv[i], "-d") == 0 ) {
          if( whattodo != DoNone ) {
            quit("Only one command may be specified.");
          }
          whattodo = DoExtract;
        } else if( strcasecmp(argv[i], "-t") == 0 ) {
          if( whattodo != DoNone ) {
            quit("Only one command may be specified.");
          }
          whattodo = DoCompare;
        } else if( strcasecmp(argv[i], "-l") == 0 ) {
          if( whattodo != DoNone ) {
            quit("Only one command may be specified.");
          }
          whattodo = DoList;
        } else if( strcasecmp(argv[i], "-v") == 0 ) {
          verbose = true;
        } else if( strcasecmp(argv[i], "-log") == 0 ) {
          if( logfile.strsize() != 0 ) {
            quit("Only one logfile may be specified.");
          }
          if( ++i == argc ) {
            quit("The -log switch requires a filename.");
          }
          logfile += argv[i];
        } else if (strcasecmp(argv[i], "-loadlstm:text") == 0) {
          if (lstmLoadFilename.strsize() != 0) {
            quit("Only one LSTM load file may be specified.");
          }
          if (++i == argc) {
            quit("The -loadlstm:text switch requires a filename.");
          }
          lstmLoadFilename += argv[i];
        } else if (strcasecmp(argv[i], "-lstmonly") == 0) {
          lstmOnly = true;
          shared.SetOptionUseLSTM();
        } else if (strcasecmp(argv[i], "-savelstm:text") == 0) {
          if (lstmSaveFilename.strsize() != 0) {
            quit("Only one LSTM save file may be specified.");
          }
          if (++i == argc) {
            quit("The -savelstm:text switch requires a filename.");
          }
          lstmSaveFilename += argv[i];
        }
        else if( strcasecmp(argv[i], "-forcebinary") == 0 ) {
          shared.SetOptionDetectBlockAsBinary();
        } else if( strcasecmp(argv[i], "-forcetext") == 0 ) {
          shared.SetOptionDetectBlockAsText();
        }
        else if ( strncmp(argv[i], "-param=", 7) == 0 )
        {
          try
          {
            shared.tuning_param = std::stof(argv[i] + 7);
          }
          catch (...)
          {
            quit("Could not parse tuning param.");
          }
        }
        else if (strncmp(argv[i], "-lstmlayers=", 12) == 0)
        {
          try
          {
            shared.LstmSettings.num_layers = std::stoi(argv[i] + 12);
          }
          catch (...)
          {
            quit("Could not parse lstmlayers parameter.");
          }
          if (shared.LstmSettings.num_layers < 1 || shared.LstmSettings.num_layers > 5)
            quit("Illegal number of lstmlayers. Should be between 1 and 5.");
        }
        else if( strcasecmp(argv[i], "-simd") == 0 ) {
          if( ++i == argc ) {
            quit("The -simd switch requires an instruction set name (NONE, SSE2, AVX2, AVX512, NEON).");
          }
          if( strcasecmp(argv[i], "NONE") == 0 ) {
            simdIset = 0;
          } else if( strcasecmp(argv[i], "SSE2") == 0 ) {
            simdIset = 3;
          } else if( strcasecmp(argv[i], "SSE3") == 0 ) {
            simdIset = 4;
          } else if( strcasecmp(argv[i], "SSE41") == 0 ) {
            simdIset = 6;
          } else if( strcasecmp(argv[i], "AVX2") == 0 ) {
            simdIset = 9;
          } else if( strcasecmp(argv[i], "AVX512") == 0 ) {
            simdIset = 10;
          } else if (strcasecmp(argv[i], "NEON") == 0) {
            simdIset = 11;
          } else {
            quit("Invalid -simd option. Use -simd NONE, -simd SSE2, -simd SSE3, -simd SSE41, -simd AVX2, -simd AVX512 or -simd NEON.");
          }
        } else {
          printf("Invalid command: %s", argv[i]);
          quit();
        }
      } else { //this parameter does not begin with a dash ("-") -> it must be a folder/filename
        if( input.strsize() == 0 ) {
          input += argv[i];
          input.replaceSlashes();
        } else if( output.strsize() == 0 ) {
          output += argv[i];
          output.replaceSlashes();
        } else {
          quit("More than two file names specified. Only an input and an output is needed.");
        }
      }
    }

    if( verbose ) {
      printModules();
    }

    // Determine CPU's (and OS) support for SIMD vectorization instruction set
    int detectedSimdIset = simdDetect();
    if( simdIset == -1 ) {
      simdIset = detectedSimdIset;
    }

    // Successfully parsed command line arguments
    // Let's check their validity

    if (lstmOnly && whattodo == DoNone) {
      whattodo = DoCompress;
    }

    if( whattodo == DoNone ) {
      quit("A command switch is required: -0..-12 to compress, -d to decompress, -t to test, -l to list.");
    }
#if defined(PAQ_DIRECT_VCXPROJ_BUILD) && !defined(PAQ_ENABLE_OPENZL)
    fprintf(stderr,
      "\nBuild profile: this direct compatibility build is PAQ-only; "
      "OpenZL SAO routing is disabled. Use the canonical CMake build for "
      "the complete routed expert profile.\n");
#endif
    if( input.strsize() == 0 ) {
      printf("\nAn %s is required %s.\n", whattodo == DoCompress ? "input file or filelist" : "archive filename",
             whattodo == DoCompress ? "for compressing" : whattodo == DoExtract ? "for decompressing" : whattodo == DoCompare
                                                                                                        ? "for testing" : whattodo == DoList
                                                                                                                          ? "to list its contents"
                                                                                                                          : "");
      quit();
    }
    if( whattodo == DoList && output.strsize() != 0 ) {
      quit("The list command needs only one file parameter.");
    }

    // File list supplied?
    if( input.beginsWith("@")) {
      if( whattodo == DoCompress ) {
        shared.SetOptionMultipleFileMode();
        input.stripStart(1);
      } else {
        quit("A file list (a file name prefixed by '@') may only be specified when compressing.");
      }
    }

    int pathType = 0;

    //Logfile supplied?
    if( logfile.strsize() != 0 ) {
      if( whattodo != DoCompress ) {
        quit("A log file may only be specified for compression.");
      }
      pathType = examinePath(logfile.c_str());
      if( pathType == 2 || pathType == 4 ) {
        quit("Specified log file should be a file, not a directory.");
      }
      if( pathType == 0 ) {
        printf("\nThere is a problem with the log file: %s", logfile.c_str());
        quit();
      }
    }

    // Validate LSTM parameters
    if (lstmLoadFilename.strsize() != 0 && !shared.GetOptionUseLSTM()) {
      quit("The -loadlstm:text switch requires the -L option to be set.");
    }
    if (lstmSaveFilename.strsize() != 0 && !shared.GetOptionUseLSTM()) {
      quit("The -savelstm:text switch requires the -L option to be set.");
    }
    if (shared.LstmSettings.num_layers > 0 && !shared.GetOptionUseLSTM()) {
      quit("Number of LSTM layers specified without enabling the LSTM model. Enable the LSTM model.");
    }
    if (shared.GetOptionUseLSTM() && shared.LstmSettings.num_layers == 0) {
      shared.LstmSettings.num_layers = 2; // default
    }

    if (verbose && shared.GetOptionUseLSTM()) {
      printf("Number of trainable parameters in LSTM model: %d\n", LstmModelContainer::GetNumberOfTrainableParameters(&shared));
    }

    // Print options in verbose mode
    if (verbose) {
      printf("\n");
      printf(" Command line   =");
      for (int i = 0; i < argc; i++) {
        printf(" %s", argv[i]);
      }
      printf("\n");
    }


    // Separate paths from input filename/directory name
    pathType = examinePath(input.c_str());
    if( pathType == 2 || pathType == 4 ) {
      printf("\nSpecified input is a directory but should be a file: %s", input.c_str());
      quit();
    }
    if( pathType == 3 ) {
      printf("\nSpecified input file does not exist: %s", input.c_str());
      quit();
    }
    if( pathType == 0 ) {
      printf("\nThere is a problem with the specified input file: %s", input.c_str());
      quit();
    }
    if( input.lastSlashPos() >= 0 ) {
      inputPath += input.c_str();
      inputPath.keepPath();
      input.keepFilename();
    }

    // Separate paths from output filename/directory name
    if( output.strsize() > 0 ) {
      pathType = examinePath(output.c_str());
      if( pathType == 1 || pathType == 3 ) { //is an existing file, or looks like a file
        if( output.lastSlashPos() >= 0 ) {
          outputPath += output.c_str();
          outputPath.keepPath();
          output.keepFilename();
        }
      } else if( pathType == 2 || pathType == 4 ) {//is an existing directory, or looks like a directory
        outputPath += output.c_str();
        if( !outputPath.endsWith("/") && !outputPath.endsWith("\\")) {
          outputPath += GOODSLASH;
        }
        //output file is not specified
        output.resize(0);
        output.pushBack(0);
      } else {
        printf("\nThere is a problem with the specified output: %s", output.c_str());
        quit();
      }
    }

    //determine archive name
    if( whattodo == DoCompress ) {
      archiveName += outputPath.c_str();
      if( output.strsize() == 0 ) { // If no archive name is provided, construct it from input (append PROGNAME extension to input filename)
        archiveName += input.c_str();
        archiveName += "." PROGNAME PROGVERSION;
      } else {
        archiveName += output.c_str();
      }
    } else { // extract/compare/list: archivename is simply the input
      archiveName += inputPath.c_str();
      archiveName += input.c_str();
    }
    if( verbose ) {
      printf(" Archive        = %s\n", archiveName.c_str());
      printf(" Input folder   = %s\n", inputPath.strsize() == 0 ? "." : inputPath.c_str());
      printf(" Output folder  = %s\n", outputPath.strsize() == 0 ? "." : outputPath.c_str());
    }

    int c = 0;

    Mode mode = whattodo == DoCompress ? COMPRESS : DECOMPRESS;

    ListOfFiles listoffiles;

    // set basePath for file list
    listoffiles.setBasePath(whattodo == DoCompress ? inputPath.c_str() : outputPath.c_str());

    // Process file list (in multiple file mode)
    if(shared.GetOptionMultipleFileMode()) { //multiple file mode
      assert(whattodo == DoCompress);
      // Read and parse file list file
      FileDisk f;
      FileName fn(inputPath.c_str());
      fn += input.c_str();
      f.open(fn.c_str(), true);
      while( true ) {
        c = f.getchar();
        listoffiles.addChar(c);
        if( c == EOF) {
          break;
        }
      }
      f.close();
      // Freeze the same archived-name contract used by extraction before
      // opening any listed member or creating the archive. This prevents the
      // encoder from emitting a legacy multi-file archive that its decoder
      // must reject or that maps two logical entries to one output target.
      const std::filesystem::path archivedListTarget =
        validateArchivedRelativePath(input.c_str(), "file-list filename");
      std::vector<std::filesystem::path> archivedMemberTargets;
      archivedMemberTargets.reserve(
        static_cast<size_t>(listoffiles.getCount()));
      for (int index = 0; index < listoffiles.getCount(); ++index) {
        const char* archivedMember = listoffiles.getRelativeFilename(index);
        const std::filesystem::path memberTarget =
          validateArchivedRelativePath(archivedMember, "member filename");
        if (archivedTargetsConflict(archivedListTarget, memberTarget))
          rejectArchivedPath("member filename", archivedMember,
                             "the target conflicts with the file-list output");
        for (const std::filesystem::path& previous : archivedMemberTargets) {
          if (archivedTargetsConflict(previous, memberTarget))
            rejectArchivedPath(
              "member filename", archivedMember,
              "the target conflicts with another archive member");
        }
        archivedMemberTargets.push_back(memberTarget);
      }
      //Verify input files
      for( int i = 0; i < listoffiles.getCount(); i++ ) {
        getFileSize(listoffiles.getfilename(i)); // Does file exist? Is it readable? (we don't actually need the file size now)
      }
    } else { //single file mode or extract/compare/list
      FileName fn(inputPath.c_str());
      fn += input.c_str();
      getFileSize(fn.c_str()); // Does file exist? Is it readable? (we don't actually need the file size now)
    }

    FileDisk archive;       // outer hybrid archive, or direct legacy input
    FileTmp codecPayload;   // complete legacy PAQ archive carried by the hybrid frame
    File* codecArchive = &archive;
    const char* inputArchiveExtension = "." PROGNAME PROGVERSION;
    bool nativeRoutedDecode = false;
    bool nativeRoutedEncode = false;

    if( mode == DECOMPRESS ) {
      archive.open(archiveName.c_str(), true);
      const bool isRoutedArchive = routed::hasRoutedArchiveMagic(&archive);
      const bool isHybridArchive = !isRoutedArchive && hybrid::hasArchiveMagic(&archive);
      if (isRoutedArchive) {
        archive.setpos(0);
        const routed::RoutedArchiveLayout layout =
          routed::inspectRoutedArchiveLayout(&archive);
        if (layout.legacySingleArchive) {
          archive.setpos(0);
          routed::readSingleLegacyArchive(&archive, &codecPayload);
          codecArchive = &codecPayload;
        }
        else {
          nativeRoutedDecode = true;
          archive.setpos(0);
          codecArchive = nullptr;
        }
      }
      else if (isHybridArchive) {
        archive.setpos(0);
        hybrid::readPaqLegacyArchive(&archive, &codecPayload);
        codecArchive = &codecPayload;
        inputArchiveExtension =
          "." LEGACY_HYBRID_PROGNAME LEGACY_HYBRID_PROGVERSION;
      }
      else {
        archive.setpos(0);
        codecArchive = &archive;
      }

      if (!nativeRoutedDecode) {
        const LegacyArchiveFlavor legacyFlavor =
            readLegacyArchiveSettings(codecArchive, level, shared);
        if (!isRoutedArchive && !isHybridArchive)
          inputArchiveExtension = legacyArchiveExtension(legacyFlavor);
      }
    }

    const bool hasExternalModelState =
      lstmLoadFilename.strsize() != 0 || lstmSaveFilename.strsize() != 0;
    if (mode == COMPRESS) {
      nativeRoutedEncode = routed::nativeRoutedEncodingAllowed(
        shared, static_cast<uint8_t>(level),
        shared.GetOptionMultipleFileMode(), hasExternalModelState);
    }
    if (nativeRoutedDecode && hasExternalModelState)
      quit("Native routed archives do not accept external LSTM model state.");

    // Native routed-v2 archives freeze the scalar PAQ implementation through
    // their v1 PAQ-fragment decoder contract. SIMD selection remains a
    // legacy-path runtime choice and must not reject or silently alter a
    // native archive.
    if (nativeRoutedEncode || nativeRoutedDecode) {
      shared.chosenSimd = SIMDType::SIMD_NONE;
      if (verbose || simdIset != detectedSimdIset)
        printf("\nNative routed-v2 uses the frozen non-vectorized PAQ backend.\n");
    }
    else {
      if (verbose || simdIset != detectedSimdIset)
        printSimdInfo(simdIset, detectedSimdIset);

      if (simdIset == 11)
        shared.chosenSimd = SIMDType::SIMD_NEON;
      else if (simdIset >= 10)
        shared.chosenSimd = SIMDType::SIMD_AVX512;
      else if (simdIset >= 9)
        shared.chosenSimd = SIMDType::SIMD_AVX2;
      else if (simdIset >= 6)
        shared.chosenSimd = SIMDType::SIMD_SSE41;
      else if (simdIset >= 4)
        shared.chosenSimd = SIMDType::SIMD_SSE3;
      else if (simdIset >= 3)
        shared.chosenSimd = SIMDType::SIMD_SSE2;
      else
        shared.chosenSimd = SIMDType::SIMD_NONE;

      if (!IS_ARM_NEON_AVAILABLE &&
          shared.chosenSimd == SIMDType::SIMD_NEON)
        quit("The ARM Neon instruction set is not available on this platform.");
      if (!IS_X64_SIMD_AVAILABLE && (
          shared.chosenSimd == SIMDType::SIMD_SSE2 ||
          shared.chosenSimd == SIMDType::SIMD_SSE3 ||
          shared.chosenSimd == SIMDType::SIMD_SSE41 ||
          shared.chosenSimd == SIMDType::SIMD_AVX2 ||
          shared.chosenSimd == SIMDType::SIMD_AVX512))
        quit("The x64 SIMD instruction set is not available on this platform.");
      if (simdIset > detectedSimdIset)
        printf("\nOverriding system highest vectorization support. Expect a crash.");
    }

    // Load LSTM model if requested
    if (lstmLoadFilename.strsize() != 0) {
      printf("Loading LSTM model from %s", lstmLoadFilename.c_str());
      FILE* lstmFile = fopen(lstmLoadFilename.c_str(), "rb");
      if (!lstmFile) {
        printf("\nError: Cannot open LSTM model file: %s\n", lstmLoadFilename.c_str());
        quit();
      }
      Models models = Models(&shared, nullptr);
      LstmModelContainer& lstmModel = models.lstmModelText();
      lstmModel.LoadModelParameters(lstmFile);
      fclose(lstmFile);
      printf(" ... Done.\n");
    }

    if( verbose ) {
      printCommand(whattodo);
      if (!nativeRoutedDecode)
        printOptions(&shared, level);
    }
    printf("\n");

    int numberOfFiles = 1; //default for single file mode

    // Write archive header to archive file
    if( mode == COMPRESS ) {
      if(shared.GetOptionMultipleFileMode()) { //multiple file mode
        numberOfFiles = listoffiles.getCount();
        printf("Creating archive %s in multiple file mode with %d file%s...\n", archiveName.c_str(), numberOfFiles,
               numberOfFiles > 1 ? "s" : "");
        FileName listSourceName(inputPath.c_str());
        listSourceName += input.c_str();
        requireDistinctInputAndOutput(
          listSourceName.c_str(), archiveName.c_str(),
          "Refusing to overwrite the multiple-file input list with its archive.");
        for (int index = 0; index < numberOfFiles; ++index) {
          requireDistinctInputAndOutput(
            listoffiles.getfilename(index), archiveName.c_str(),
            "Refusing to overwrite an input member with its archive.");
        }
      } else { //single file mode
        printf("Creating archive %s in single file mode...\n", archiveName.c_str());
        FileName sourceName(inputPath.c_str());
        sourceName += input.c_str();
        requireDistinctInputAndOutput(
          sourceName.c_str(), archiveName.c_str(),
          "Refusing to overwrite the input file with its archive.");
      }
      archive.create(archiveName.c_str());
      if (nativeRoutedEncode) {
        codecArchive = &archive;
      }
      else {
        codecArchive = &codecPayload;
        codecArchive->append(ROUTED_STRUCTURED_PROGNAME);
        codecArchive->putChar(level);
        codecArchive->putChar(shared.options);
        if(shared.GetOptionUseLSTM())
          codecArchive->putChar(shared.LstmSettings.num_layers);
      }
    }

    // In single file mode with no output filename specified we must construct it from the supplied archive filename
    if(!shared.GetOptionMultipleFileMode()) { //single file mode
      if((whattodo == DoExtract || whattodo == DoCompare) && output.strsize() == 0 ) {
        output += input.c_str();
        const char *fileExtension = inputArchiveExtension;
        if( output.endsWith(fileExtension)) {
          output.stripEnd(static_cast<int>(strlen(fileExtension)));
        } else {
          printf("Can't construct output filename from archive filename.\nArchive file extension must be: '%s'", fileExtension);
          quit();
        }
      }
      if (mode == DECOMPRESS && whattodo == DoExtract) {
        FileName outputName(outputPath.c_str());
        outputName += output.c_str();
        requireDistinctInputAndOutput(
          archiveName.c_str(), outputName.c_str(),
          "Refusing to overwrite the input archive with decoded output.");
      }
    }

    if (lstmOnly && level > 0) {
      quit("The -lstmonly option is not compatible with specifying a compression level.");
    }

    // Native routed archives must dispatch before constructing any legacy
    // Predictor/Models object. Several PAQ model accessors use function-static
    // storage bound to the first Shared instance in the process.
    if (nativeRoutedEncode) {
      FileName sourceName(inputPath.c_str());
      sourceName += input.c_str();
      const uint64_t sourceSize = getFileSize(sourceName.c_str());
      if (!shared.toScreen)
        fprintf(stderr, "\nFilename: %s (%" PRIu64 " bytes)\n",
                sourceName.c_str(), sourceSize);
      printf("\nFilename: %s (%" PRIu64 " bytes)\n",
             sourceName.c_str(), sourceSize);

      const routed::NativeRoutedStats stats = routed::encodeNativeSingleFile(
        &archive, sourceName.c_str(), sourceSize, shared,
        static_cast<uint8_t>(level), hasExternalModelState);
      printf("-----------------------\n");
      printf("Total input size     : %" PRIu64 "\n", stats.sourceBytes);
      printf("Total archive size   : %" PRIu64 "\n", stats.archiveBytes);
      printf("Routed segments      : %u (PAQ %u, external %u)\n\n",
             stats.segmentCount, stats.paqFragmentCount,
             stats.externalExpertCount);
      writeCompressionLog(logfile, argc, argv, shared, input,
                          stats.sourceBytes, stats.archiveBytes,
                          programChecker->getRuntime());
      archive.close();
      programChecker->print();
      return 0;
    }

    if (nativeRoutedDecode) {
      if (whattodo == DoList)
        quit("Native single-file routed archives do not store a file list.");
      if (whattodo != DoExtract && whattodo != DoCompare)
        quit("Native routed archives require extract or compare mode.");
      if (shared.GetOptionUseLSTM() || shared.GetOptionTrainExe() ||
          shared.GetOptionTrainTxt())
        quit("Native routed decoding is controlled only by archived model state.");

      FileName outputName(outputPath.c_str());
      outputName += output.c_str();
      FileDisk nativeOutput;
      const FMode outputMode = whattodo == DoExtract
        ? FMode::FDECOMPRESS : FMode::FCOMPARE;
      if (outputMode == FMode::FDECOMPRESS) {
        nativeOutput.create(outputName.c_str());
        printf("Extracting");
      }
      else {
        nativeOutput.open(outputName.c_str(), true);
        printf("Comparing");
      }
      printf(" %s -> ", outputName.c_str());
      const routed::NativeRoutedStats stats = routed::decodeNativeSingleFile(
        &archive, &nativeOutput, outputMode, shared);
      if (outputMode == FMode::FCOMPARE && stats.firstDifference != 0)
        printf("differ at %" PRIu64 "\n", stats.firstDifference - 1);
      else if (outputMode == FMode::FCOMPARE && stats.comparedFileIsLonger)
        printf("file is longer\n");
      else if (outputMode == FMode::FCOMPARE)
        printf("identical\n");
      else
        printf("done (%" PRIu64 " bytes)\n", stats.sourceBytes);
      if (verbose) {
        printf(" Routed segments = %u (PAQ %u, external %u)\n",
               stats.segmentCount, stats.paqFragmentCount,
               stats.externalExpertCount);
      }
      nativeOutput.close();
      archive.close();
      programChecker->print();
      return 0;
    }

    shared.init(level);

    bool useCm = level != 0;
    bool useLstm = shared.GetOptionUseLSTM();
    bool doEncoding = useCm || useLstm;

    Shared sharedForBlockPredictor;
    std::unique_ptr<Predictor> predictorBlock = nullptr;
    std::unique_ptr<Predictor> predictorMain = nullptr;

    if (doEncoding) {
      sharedForBlockPredictor.init(level, 16);
      sharedForBlockPredictor.chosenSimd = shared.chosenSimd;
      predictorBlock = std::make_unique<PredictorBlock>(&sharedForBlockPredictor);
      if (level == 0 && useLstm) {
        predictorMain = std::make_unique<PredictorMainLstmOnly>(&shared);
      }
      else {
        predictorMain = std::make_unique<PredictorMain>(&shared);
      }
    }

    Encoder en(predictorBlock.get(), predictorMain.get(), doEncoding, mode,
               codecArchive, &shared);
    uint64_t contentSize = 0;
    uint64_t totalSize = 0;
    uint64_t finalArchiveSize = 0;

    // Compress list of files
    if( mode == COMPRESS ) {
      uint64_t start = en.size(); //inner legacy PAQ header size
      if( verbose ) {
        printf("Writing header : %" PRIu64 " bytes\n", start);
      }
      totalSize += start;
      if(shared.GetOptionMultipleFileMode()) { //multiple file mode

        uint64_t len1 = input.size(); //ASCIIZ filename of listfile - with ending zero
        const String *const s = listoffiles.getString();
        uint64_t len2 = s->size(); //ASCIIZ filenames of files to compress - with ending zero
        Block::EncodeBlockHeader(&en, BlockType::TEXT, len1 + len2, 0);

        for( uint64_t i = 0; i < len1; i++ ) {
          en.compressByte(predictorMain.get(), input[i]); //ASCIIZ filename of listfile
        }
        for( uint64_t i = 0; i < len2; i++ ) {
          en.compressByte(predictorMain.get(), (*s)[i]); //ASCIIZ filenames of files to compress
        }

        printf("1/2 - Filename of listfile : %" PRIu64 " bytes\n", len1);
        printf("2/2 - Content of listfile  : %" PRIu64 " bytes\n", len2);
        printf("----- Compressed to        : %" PRIu64 " bytes\n", en.size() - start);
        totalSize += len1 + len2;
      }
    }

    // Decompress list of files
    if( mode == DECOMPRESS && shared.GetOptionMultipleFileMode()) {
      const char *errmsgInvalidChar = "Invalid character or unexpected end of archive file.";
      // name of listfile
      FileName archivedListFilename;
      FileName listFilename;
      if( output.strsize() != 0 ) {
        quit("Output filename must not be specified when extracting multiple files.");
      }
      Block::DecodeBlockHeader(&en);
      if(shared.State.blockType != BlockType::TEXT ) {
        quit(errmsgInvalidChar);
      }
      while((c = en.decompressByte(predictorMain.get())) != 0 ) {
        if( c == 255 ) {
          quit(errmsgInvalidChar);
        }
        archivedListFilename += static_cast<char>(c);
      }
      while((c = en.decompressByte(predictorMain.get())) != 0 ) {
        if( c == 255 ) {
          quit(errmsgInvalidChar);
        }
        listoffiles.addChar(c);
      }
      listoffiles.addChar(EOF);

      numberOfFiles = listoffiles.getCount();
      if (whattodo == DoList) {
        // Listing has no filesystem target, but malformed archive spellings
        // are still rejected deterministically.
        validateArchivedRelativePath(archivedListFilename.c_str(),
                                     "file-list filename");
        for (int index = 0; index < numberOfFiles; ++index) {
          validateArchivedRelativePath(
            listoffiles.getRelativeFilename(index), "member filename");
        }
      }
      else {
        // Resolve every archive-controlled path and reject all conflicts
        // before the first create/open. Ordinary relative legacy names retain
        // their directory layout, but no target may escape the output root,
        // alias the archive, collide with the list file or another member, or
        // be an ancestor/descendant of another output file.
        const std::filesystem::path extractionRoot =
          strictOutputRoot(outputPath.c_str());
        const std::string resolvedListFilename = validatedExtractionTarget(
          extractionRoot, archivedListFilename.c_str(), archiveName.c_str(),
          "file-list filename");
        listFilename += resolvedListFilename.c_str();
        const std::filesystem::path listTarget =
          std::filesystem::u8path(resolvedListFilename);
        for (int index = 0; index < numberOfFiles; ++index) {
          const char* archivedMember =
            listoffiles.getRelativeFilename(index);
          const std::string resolvedMember = validatedExtractionTarget(
            extractionRoot, archivedMember, archiveName.c_str(),
            "member filename");
          const std::filesystem::path memberTarget =
            std::filesystem::u8path(resolvedMember);
          if (extractionTargetsConflict(listTarget, memberTarget))
            rejectArchivedPath("member filename", archivedMember,
                               "the target conflicts with the file-list output");
          for (int previous = 0; previous < index; ++previous) {
            if (extractionTargetsConflict(
                  std::filesystem::u8path(listoffiles.getfilename(previous)),
                  memberTarget))
              rejectArchivedPath(
                "member filename", archivedMember,
                "the target conflicts with another archive member");
          }
          listoffiles.setResolvedFilename(index, resolvedMember.c_str());
        }
      }

      if( whattodo == DoList ) {
        printf("File list of %s archive:\n", archiveName.c_str());
      }

      //write filenames to screen or listfile or verify (compare) contents
      if( whattodo == DoList ) {
        printf("%s\n", listoffiles.getString()->c_str());
      } else if( whattodo == DoExtract ) {
        FileDisk f;
        f.create(listFilename.c_str());
        String *s = listoffiles.getString();
        f.blockWrite(reinterpret_cast<uint8_t *>(&(*s)[0]), s->strsize());
        f.close();
      } else if( whattodo == DoCompare ) {
        FileDisk f;
        f.open(listFilename.c_str(), true);
        String *s = listoffiles.getString();
        for( uint64_t i = 0; i < s->strsize(); i++ ) {
          if( f.getchar() != static_cast<uint8_t>((*s)[i])) {
            quit("Mismatch in list of files.");
          }
        }
        if( f.getchar() != EOF) {
          printf("Filelist on disk is larger than in archive.\n");
        }
        f.close();
      }
    }

    if( whattodo == DoList && !shared.GetOptionMultipleFileMode()) {
      quit("Can't list. Filenames are not stored in single file mode.\n");
    }

    // Compress or decompress files
    if( mode == COMPRESS ) {
      if( !shared.toScreen ) { //we need a minimal feedback when redirected
        fprintf(stderr, "Output is redirected - only minimal feedback is on screen\n");
      }
      if(shared.GetOptionMultipleFileMode()) { //multiple file mode
        for( int i = 0; i < numberOfFiles; i++ ) {
          const char *fName = listoffiles.getfilename(i);
          uint64_t fSize = getFileSize(fName);
          if( !shared.toScreen ) { //we need a minimal feedback when redirected
            fprintf(stderr, "\n%d/%d - Filename: %s (%" PRIu64 " bytes)\n", i + 1, numberOfFiles, fName, fSize);
          }
          printf("\n%d/%d - Filename: %s (%" PRIu64 " bytes)\n", i + 1, numberOfFiles, fName, fSize);
          compressfile(&shared, fName, fSize, en, verbose);
          totalSize += fSize + 4; //4: file size information
          contentSize += fSize;
        }
      } else { //single file mode
        FileName fn;
        fn += inputPath.c_str();
        fn += input.c_str();
        const char *fName = fn.c_str();
        uint64_t fSize = getFileSize(fName);
        if( !shared.toScreen ) { //we need a minimal feedback when redirected
          fprintf(stderr, "\nFilename: %s (%" PRIu64 " bytes)\n", fName, fSize);
        }
        printf("\nFilename: %s (%" PRIu64 " bytes)\n", fName, fSize);
        compressfile(&shared, fName, fSize, en, verbose);
        totalSize += fSize + 4; //4: file size information
        contentSize += fSize;
      }

      uint64_t preFlush = en.size();
      en.flush();
      totalSize += en.size() - preFlush; //we consider padding bytes as auxiliary bytes
      const uint64_t legacyArchiveSize = en.size();
      routed::writeSingleLegacyArchive(&archive, codecArchive);
      finalArchiveSize = archive.curPos();
      if (finalArchiveSize < legacyArchiveSize)
        quit("Routed archive framing produced an invalid total length.");
      totalSize += finalArchiveSize - legacyArchiveSize;
      printf("-----------------------\n");
      printf("Total input size     : %" PRIu64 "\n", contentSize);
      if( verbose ) {
        printf("Total metadata bytes : %" PRIu64 "\n", totalSize - contentSize);
      }
      printf("Total archive size   : %" PRIu64 "\n", finalArchiveSize);
      printf("\n");
      // Log compression results
      if( logfile.strsize() != 0 ) {
        bool showParam = shared.tuning_param != 0.0f;
        String results;
        pathType = examinePath(logfile.c_str());
        //Write header if needed
        if( pathType == 3 /*does not exist*/ ||
            (pathType == 1 && getFileSize(logfile.c_str()) == 0)/*exists but does not contain a header*/) {
          results += "PROG_NAME\tPROG_VERSION\tCOMMAND_LINE\tLEVEL\t";
            if (showParam)
              results += "PARAM\t";
          results +=  "INPUT_FILENAME\tORIGINAL_SIZE_BYTES\tCOMPRESSED_SIZE_BYTES\tRUNTIME_MS\n";
        }
        //Write results to logfile
        results += PROGNAME "\t" PROGVERSION "\t";
        for( int i = 1; i < argc; i++ ) {
          if( i != 0 ) {
            results += ' ';
          }
          results += argv[i];
        }
        results += "\t";
        results += uint64_t(shared.level);
        results += "\t";
        if (showParam) {
          results += std::to_string(shared.tuning_param).c_str();
          results += "\t";
        }
        results += input.c_str();
        results += "\t";
        results += contentSize;
        results += "\t";
        results += finalArchiveSize;
        results += "\t";
        results += uint64_t(programChecker->getRuntime() * 1000.0);
        results += "\t";
        results += "\n";
        appendToFile(logfile.c_str(), results.c_str());
        printf("Results logged to file '%s'\n", logfile.c_str());
        printf("\n");
      }
    } else { //decompress
      if( whattodo == DoExtract || whattodo == DoCompare ) {
        FMode fMode = whattodo == DoExtract ? FMode::FDECOMPRESS : FMode::FCOMPARE;
        if(shared.GetOptionMultipleFileMode()) { //multiple file mode
          for( int i = 0; i < numberOfFiles; i++ ) {
            const char *fName = listoffiles.getfilename(i);
            decompressFile(&shared, fName, fMode, en);
          }
        } else { //single file mode
          FileName fn;
          fn += outputPath.c_str();
          fn += output.c_str();
          const char *fName = fn.c_str();
          decompressFile(&shared, fName, fMode, en);
        }
      }
    }

    archive.close();

    // Save LSTM model if requested
    if (lstmSaveFilename.strsize() != 0) {
      printf("\nSaving LSTM model parameters to %s", lstmSaveFilename.c_str());
      FILE* lstmFile = fopen(lstmSaveFilename.c_str(), "wb");
      if (!lstmFile) {
        printf("\nError: Cannot create LSTM model file: %s\n", lstmSaveFilename.c_str());
        quit();
      }
      Models models = Models(&shared, nullptr);
      LstmModelContainer& lstmModel = models.lstmModelText();
      lstmModel.SaveModelParameters(lstmFile);
      fclose(lstmFile);
      printf(" ... Done.\n");
    }

    if( whattodo != DoList ) {
      programChecker->print();
    }
  }
    // we catch only the intentional exceptions from quit() to exit gracefully
    // any other exception should result in a crash and must be investigated
  catch( IntentionalException const & ) {
  }

  return 0;
}

#ifdef WINDOWS
#include "shellapi.h"
#pragma comment(lib,"shell32.lib")
#endif

int main(int argc, char **argv) {
#ifdef WINDOWS
  // On Windows, argv is encoded in the effective codepage, therefore unsuitable for acquiring command line arguments (file names
  // in our case) not representable in that particular codepage.
  // -> We will recreate argv as UTF8 (like in Linux)
  uint32_t oldcp = GetConsoleOutputCP();
  SetConsoleOutputCP(CP_UTF8);
  wchar_t **szArglist;
  int argc_utf8;
  char** argv_utf8;
  if( (szArglist = CommandLineToArgvW(GetCommandLineW(), &argc_utf8)) == NULL) {
    printf("CommandLineToArgvW failed\n");
    return 0;
  } else {
    if(argc!=argc_utf8)quit("Error preparing command line arguments.");
    argv_utf8=new char*[argc_utf8+1];
    for(int i=0; i<argc_utf8; i++) {
      wchar_t *s=szArglist[i];
      int buffersize = WideCharToMultiByte(CP_UTF8,0,s,-1,NULL,0,NULL,NULL);
      argv_utf8[i] = new char[buffersize];
      WideCharToMultiByte(CP_UTF8,0,s,-1,argv_utf8[i],buffersize,NULL,NULL);
      //printf("%d: %s\n", i, argv_utf8[i]); //debug: see if conversion is successful
    }
    argv_utf8[argc_utf8]=nullptr;
    int retval=processCommandLine(argc_utf8, argv_utf8);
    for(int i=0; i<argc_utf8; i++)
      delete[] argv_utf8[i];
    delete[] argv_utf8;
    SetConsoleOutputCP(oldcp);
    return retval;
  }
#else
  return processCommandLine(argc, argv);
#endif
}
