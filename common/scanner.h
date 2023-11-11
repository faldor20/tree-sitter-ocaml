#ifndef TREE_SITTER_OCAML_SCANNER_H_
#define TREE_SITTER_OCAML_SCANNER_H_

#define QUOTED_STRING_DEPTH 20

#include <assert.h>
#include <string.h>
#include <tree_sitter/parser.h>
#include <wctype.h>

enum TokenType {
  STRING_DELIM,
  LINE_NUMBER_DIRECTIVE,
  NULL_CHARACTER,
};

enum ParserState {
  IN_QUOTED_STRING,
  IN_STRING,
  IN_COMMENT,

};
typedef struct {
  size_t id_length;
  ParserState state;
  char id[100];
} State;

typedef struct {
  size_t state_depth;

  State *states[QUOTED_STRING_DEPTH];
} Scanner;

static inline State *get_current_state(Scanner *scanner) {
  return scanner->states[scanner->state_depth - 1];
}

static inline void quoted_string_id_clear(State *quoted_string) {
  quoted_string->id_length = 0;
}

static inline void quoted_string_id_push(Scanner *scanner, char c) {
  scanner->quoted_strings_count += 1;
  Quoted_string *quoted_string = get_current_quoted_string(scanner);
  quoted_string_id_resize(quoted_string, quoted_string->id_length + 1);
  quoted_string->id[quoted_string->id_length++] = c;
}

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static inline void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static inline bool eof(TSLexer *lexer) { return lexer->eof(lexer); }

static void scan_string(TSLexer *lexer) {
  for (;;) {
    switch (lexer->lookahead) {
      case '\\':
        advance(lexer);
        advance(lexer);
        break;
      case '"':
        advance(lexer);
        return;
      case '\0':
        if (eof(lexer)) return;
        advance(lexer);
        break;
      default:
        advance(lexer);
    }
  }
}

static bool scan_left_quoted_string_delimiter(Scanner *scanner,
                                              TSLexer *lexer) {
  scanner->quoted_strings_count++;
  Quoted_string *quoted_string = get_current_quoted_string(scanner);
  // Ensure we remove any leftovers from previous uses
  quoted_string_id_clear(quoted_string);

  while (iswlower(lexer->lookahead) || lexer->lookahead == '_') {
    quoted_string_id_push(scanner, lexer->lookahead);
    advance(lexer);
  }

  if (lexer->lookahead != '|') {
    scanner->quoted_strings_count--;
    return false;
  }

  advance(lexer);
  return true;
}

static bool scan_right_quoted_string_delimiter(Scanner *scanner,
                                               TSLexer *lexer) {
  Quoted_string *quoted_string = get_current_quoted_string(scanner);
  for (size_t i = 0; i < quoted_string->id_length; i++) {
    if (lexer->lookahead != quoted_string->id[i]) return false;
    advance(lexer);
  }
  if (lexer->lookahead != '}') return false;

  scanner->quoted_strings_count -= 1;
  return true;
}
static bool scan_right_interpolation_delim(Scanner *scanner, TSLexer *lexer) {
  scanner->in_interpolation = false;
  return true;
}
static bool scan_left_interpolation_delim(Scanner *scanner, TSLexer *lexer) {
  if (iswupper(lexer->lookahead)) {
    advance(lexer);
    while (iswlower(lexer->lookahead)) {
      advance(lexer);
    }
  }

  if (lexer->lookahead != '{') return false;

  advance(lexer);
  return true;
}

static bool scan_interpolation(Scanner *scanner, TSLexer *lexer) {
  if (!scan_left_interpolation_delim(scanner, lexer)) return false;
  scanner->in_interpolation = true;

  for (;;) {
    switch (lexer->lookahead) {
      // TODO this should block while we scan all the consents of the
      // interpolation and then re can return
      break;
      case '}':
        scanner->in_interpolation = false;
        advance(lexer);
        return true;
        break;
      case '\0':
        if (eof(lexer)) return false;
        advance(lexer);
        break;
      default:
        advance(lexer);
    }
  }
}

static bool scan_quoted_string(Scanner *scanner, TSLexer *lexer) {
  if (!scan_left_quoted_string_delimiter(scanner, lexer)) return false;

  for (;;) {
    switch (lexer->lookahead) {
      // TODO this should block while we scan all the consents of the
      // interpolation and then re can return
      case '%':
        scan_interpolation(scanner, lexer);
        break;
      case '|':
        advance(lexer);
        if (scan_right_quoted_string_delimiter(scanner, lexer)) return true;
        break;
      case '\0':
        if (eof(lexer)) return false;
        advance(lexer);
        break;
      default:
        advance(lexer);
    }
  }
}

static char scan_character(TSLexer *lexer) {
  char last = 0;

  switch (lexer->lookahead) {
    case '\\':
      advance(lexer);
      if (iswdigit(lexer->lookahead)) {
        advance(lexer);
        for (size_t i = 0; i < 2; i++) {
          if (!iswdigit(lexer->lookahead)) return 0;
          advance(lexer);
        }
      } else {
        switch (lexer->lookahead) {
          case 'x':
            advance(lexer);
            for (size_t i = 0; i < 2; i++) {
              if (!iswdigit(lexer->lookahead) &&
                  (towupper(lexer->lookahead) < 'A' ||
                   towupper(lexer->lookahead) > 'F')) {
                return 0;
              }
              advance(lexer);
            }
            break;
          case 'o':
            advance(lexer);
            for (size_t i = 0; i < 3; i++) {
              if (!iswdigit(lexer->lookahead) || lexer->lookahead > '7') {
                return 0;
              }
              advance(lexer);
            }
            break;
          case '\'':
          case '"':
          case '\\':
          case 'n':
          case 't':
          case 'b':
          case 'r':
          case ' ':
            last = lexer->lookahead;
            advance(lexer);
            break;
          default:
            return 0;
        }
      }
      break;
    case '\'':
      break;
    case '\0':
      if (eof(lexer)) return 0;
      advance(lexer);
      break;
    default:
      last = lexer->lookahead;
      advance(lexer);
  }

  if (lexer->lookahead == '\'') {
    advance(lexer);
    return 0;
  }
  return last;
}

static bool scan_identifier(TSLexer *lexer) {
  if (iswalpha(lexer->lookahead) || lexer->lookahead == '_') {
    advance(lexer);
    while (iswalnum(lexer->lookahead) || lexer->lookahead == '_' ||
           lexer->lookahead == '\'') {
      advance(lexer);
    }
    return true;
  }
  return false;
}

static bool scan_extattrident(TSLexer *lexer) {
  while (scan_identifier(lexer)) {
    if (lexer->lookahead != '.') return true;
    advance(lexer);
  }
  return false;
}

static bool scan_comment(Scanner *scanner, TSLexer *lexer) {
  char last = 0;

  if (lexer->lookahead != '*') return false;
  advance(lexer);

  for (;;) {
    switch (last ? last : lexer->lookahead) {
      case '(':
        if (last) {
          last = 0;
        } else {
          advance(lexer);
        }
        scan_comment(scanner, lexer);
        break;
      case '*':
        if (last) {
          last = 0;
        } else {
          advance(lexer);
        }
        if (lexer->lookahead == ')') {
          advance(lexer);
          return true;
        }
        break;
      case '\'':
        if (last) {
          last = 0;
        } else {
          advance(lexer);
        }
        last = scan_character(lexer);
        break;
      case '"':
        if (last) {
          last = 0;
        } else {
          advance(lexer);
        }
        scan_string(lexer);
        break;
      case '{':
        if (last) {
          last = 0;
        } else {
          advance(lexer);
        }
        if (lexer->lookahead == '%') {
          advance(lexer);
          if (lexer->lookahead == '%') advance(lexer);
          if (scan_extattrident(lexer)) {
            while (iswspace(lexer->lookahead)) advance(lexer);
          } else {
            break;
          }
        }
        if (scan_quoted_string(scanner, lexer)) advance(lexer);
        break;
      case '\0':
        if (eof(lexer)) return false;
        if (last) {
          last = 0;
        } else {
          advance(lexer);
        }
        break;
      default:
        if (scan_identifier(lexer) || last) {
          last = 0;
        } else {
          advance(lexer);
        }
    }
  }
}

static Scanner *create() {
  Scanner *scanner = calloc(1, sizeof(Scanner));
  for (size_t i = 0; i < QUOTED_STRING_DEPTH; i++) {
    scanner->quoted_strings[i] = calloc(1, sizeof(Quoted_string));
  }
  return scanner;
}

static void destroy(Scanner *scanner) {
  for (size_t i = 0; i < QUOTED_STRING_DEPTH; i++) {
    free(scanner->quoted_strings[i]->id);
    free(scanner->quoted_strings[i]);
  }
  free(scanner);
}
static size_t total_quoted_string_length(Scanner *scanner) {
  size_t length = 0;
  for (size_t i = 0; i < scanner->quoted_strings_count; i++) {
    Quoted_string *quoted_string = scanner->quoted_strings[i];
    length += quoted_string->id_length;
  }
  return length;
}

static unsigned serialize(Scanner *scanner, char *buffer) {
  buffer[0] = scanner->quoted_strings_count;
  buffer[1] = scanner->in_string;
  buffer[2] = scanner->in_interpolation;

  size_t length = total_quoted_string_length(scanner);
  if (length >= TREE_SITTER_SERIALIZATION_BUFFER_SIZE) {
    return 1;
  }
  return quoted_string_ids_copy(scanner, buffer + 3) + 3;
}

static void deserialize(Scanner *scanner, const char *buffer, unsigned length) {
  if (length > 0) {
    scanner->quoted_strings_count = buffer[0];
    scanner->in_string = buffer[1];
    scanner->in_interpolation = buffer[2];
    quoted_strings_assign(scanner, buffer + 3, length - 3);
  } else {
    scanner->in_interpolation = false;
    scanner->in_string = false;
    scanner->quoted_strings_count = 0;
  }
}

static bool scan(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols) {
  if (scanner->in_string && valid_symbols[STRING_DELIM] &&
      lexer->lookahead == '"') {
    advance(lexer);
    scanner->in_string = false;
    lexer->result_symbol = STRING_DELIM;
    return true;
  }

  while (iswspace(lexer->lookahead)) {
    skip(lexer);
  }

  if (!in_any_string(scanner) && lexer->lookahead == '#' &&
      lexer->get_column(lexer) == 0) {
    advance(lexer);

    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
      advance(lexer);
    }

    if (!iswdigit(lexer->lookahead)) return false;
    while (iswdigit(lexer->lookahead)) advance(lexer);

    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
      advance(lexer);
    }

    if (lexer->lookahead != '"') return false;
    advance(lexer);

    while (lexer->lookahead != '\n' && lexer->lookahead != '\r' &&
           lexer->lookahead != '"' && !eof(lexer)) {
      advance(lexer);
    }

    if (lexer->lookahead != '"') return false;
    advance(lexer);

    while (lexer->lookahead != '\n' && lexer->lookahead != '\r' &&
           !eof(lexer)) {
      advance(lexer);
    }

    lexer->result_symbol = LINE_NUMBER_DIRECTIVE;
    return true;
  }
  if (!in_any_string(scanner) && lexer->lookahead == '(') {
    advance(lexer);
    if (lexer->lookahead == '*') {
      advance(lexer);
      return false;
    }
  }
  if (!in_any_string(scanner) && valid_symbols[STRING_DELIM] &&
      lexer->lookahead == '"') {
    advance(lexer);
    scanner->in_string = true;
    lexer->result_symbol = STRING_DELIM;
    return true;
  }
  if (valid_symbols[NULL_CHARACTER] && lexer->lookahead == '\0' &&
      !eof(lexer)) {
    advance(lexer);
    lexer->result_symbol = NULL_CHARACTER;
    return true;
  }

  return false;
}

#endif  // TREE_SITTER_OCAML_SCANNER_H_
