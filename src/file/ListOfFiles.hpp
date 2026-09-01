#pragma once

#include "FileName.hpp"
#include "../Array.hpp"
#include "fileUtils.hpp"
#include <cassert>

class ListOfFiles {
private:
  enum class ParseState {
      IN_HEADER, FINISHED_A_FILENAME, FINISHED_A_LINE, PROCESSING_FILENAME
  } ;
  ParseState state; /**< parsing state */
  FileName basePath;
  String listOfFiles; /**< path/file list in first column, columns separated by tabs, rows separated by newlines, with header in 1st row */
  Array<FileName *> names; /**< all file names parsed from listOfFiles */
  Array<FileName *> relativeNames; /**< archive/list spellings before basePath is applied */
public:
  ListOfFiles();
  ~ListOfFiles();
  void setBasePath(const char *s);
  void addChar(int c);
  int getCount();
  const char* getfilename(int i);
  const char* getRelativeFilename(int i);
  void setResolvedFilename(int i, const char* filename);
  String* getString();
};
