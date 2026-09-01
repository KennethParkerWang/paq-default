#include "ListOfFiles.hpp"
#include "../CharacterNames.hpp"

ListOfFiles::ListOfFiles()
  : state(ParseState::IN_HEADER), names(0), relativeNames(0) {}

ListOfFiles::~ListOfFiles() {
  for( int i = 0; i < static_cast<int>(names.size()); i++ ) {
    delete names[i];
    delete relativeNames[i];
  }
}

void ListOfFiles::setBasePath(const char *s) {
  assert(basePath.strsize() == 0);
  basePath += s;
}

void ListOfFiles::addChar(int c) {
  if (c == 0 || c == 255) {
    quit("NUL and 0xFF bytes are not allowed in a file list.");
  }
  if( c != EOF) {
    listOfFiles += static_cast<char>(c);
  }
  if( c == 10 || c == 13 || c == EOF) { //got a newline
    state = ParseState::FINISHED_A_LINE; //empty lines / extra newlines (cr, crlf or lf) are ignored
    return;
  }
  if( state == ParseState::IN_HEADER ) {
    return; //ignore anything in header
  }
  if( state == ParseState::FINISHED_A_FILENAME ) {
    return; //ignore the rest (other columns)
  }
  if( state == ParseState::FINISHED_A_LINE ) {
    if( c == TAB ) {
      quit("Empty filenames are not allowed in the file list.");
    }
    names.pushBack(new FileName(basePath.c_str()));
    relativeNames.pushBack(new FileName());
    state = ParseState::PROCESSING_FILENAME;
    if( c == '/' || c == '\\' ) {
      quit("For security reasons absolute paths are not allowed in the file list.");
    }
  }
  if( c == TAB ) { //got a tab
    state = ParseState::FINISHED_A_FILENAME; //ignore the rest (other columns)
    return;
  }
  if( c == ':' || c == '?' || c == '*' ) {
    printf("\nIllegal character ('%c') in file list.", c);
    quit();
  }
  if( c == BADSLASH ) {
    c = GOODSLASH;
  }
  (*names[names.size() - 1]) += static_cast<char>(c);
  (*relativeNames[relativeNames.size() - 1]) += static_cast<char>(c);
}

int ListOfFiles::getCount() { return static_cast<int>(names.size()); }

const char* ListOfFiles::getfilename(int i) { return names[i]->c_str(); }

const char* ListOfFiles::getRelativeFilename(int i) {
  return relativeNames[i]->c_str();
}

void ListOfFiles::setResolvedFilename(int i, const char* filename) {
  FileName* replacement = new FileName(filename);
  delete names[i];
  names[i] = replacement;
}

String* ListOfFiles::getString() { return &listOfFiles; }
