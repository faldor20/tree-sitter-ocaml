#ifndef TREE_SITTER_OCAML_SCANNER_H_
#define TREE_SITTER_OCAML_SCANNER_H_

#define QUOTED_STRING_DEPTH 20

#include <assert.h>
#include <string.h>
#include <tree_sitter/parser.h>
#include <wctype.h>

enum TokenType {
  LEFT_INTERPOLATION_DELIM,
  RIGHT_INTERPOLATION_DELIM,
  LEFT_QUOTED_STRING_DELIM,
  RIGHT_QUOTED_STRING_DELIM,
  STRING_DELIM,
  COMMENT_START,
  COMMENT_BODY,
  LINE_NUMBER_DIRECTIVE,
  NULL_CHARACTER,
};

enum ParserState {
  IN_QUOTED_STRING,
  IN_STRING,
  IN_COMMENT,
  IN_INTERPOLATION,
  IN_NOTHING,
};

typedef struct {
  size_t id_length;
  enum ParserState state;
  char id[100];
} Scan_state;

typedef struct {
  size_t state_depth;

  Scan_state *states[QUOTED_STRING_DEPTH];
} Scanner;

static inline Scan_state *get_current_state(Scanner *scanner) {
  return scanner->states[scanner->state_depth - 1];
}
static inline void remove_state(Scanner *scanner) { scanner->state_depth--; }

static inline void add_state(Scanner *scanner, enum ParserState new_state) {
  scanner->state_depth++;
  Scan_state *state = get_current_state(scanner);
  state->state = new_state;
  state->id_length = 0;
}

static inline void quoted_string_id_clear(Scan_state *state) {
  state->id_length = 0;
}

static inline void quoted_string_id_push(Scanner *scanner, char c) {
  Scan_state *state = get_current_state(scanner);
  state->id[state->id_length++] = c;
}

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static inline void mark_end(TSLexer *lexer) { lexer->mark_end(lexer); }

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
static inline bool next_is(TSLexer *lexer, char c) {
  return lexer->lookahead == c;
}

static bool parse_left_quoted_string_delimiter(Scanner *scanner,
                                               TSLexer *lexer) {
  // Ensure we remove any leftovers from previous uses
  add_state(scanner, IN_QUOTED_STRING);

  while (iswlower(lexer->lookahead) || next_is(lexer, '_')) {
    quoted_string_id_push(scanner, lexer->lookahead);
    advance(lexer);
  }

  if (lexer->lookahead != '|') {
    remove_state(scanner);
    return false;
  }

  advance(lexer);
  return true;
}

static bool scan_right_quoted_string_delimiter(Scanner *scanner,
                                               TSLexer *lexer) {
  Scan_state *state = get_current_state(scanner);

  if (state->id_length > 0) {
    for (size_t i = 0; i < state->id_length; i++) {
      if (lexer->lookahead != state->id[i]) return false;
      advance(lexer);
    }
  }
  if (lexer->lookahead != '}') return false;
  // we don't advance here because we want to leave the '}'
  remove_state(scanner);
  return true;
}
static bool scan_left_interpolation_delim(Scanner *scanner, TSLexer *lexer) {
  // scan the Module name if it exists
  if (iswupper(lexer->lookahead)) {
    advance(lexer);
    while (iswlower(lexer->lookahead)) {
      advance(lexer);
    }
  }

  if (lexer->lookahead != '{') return false;

  advance(lexer);
  add_state(scanner, IN_INTERPOLATION);
  return true;
}

static bool scan_right_interpolation_delim(Scanner *scanner, TSLexer *lexer) {
  remove_state(scanner);
  return true;
}

static bool scan_quoted_string(Scanner *scanner, TSLexer *lexer) {
  if (!parse_left_quoted_string_delimiter(scanner, lexer)) return false;

  for (;;) {
    switch (lexer->lookahead) {
      // TODO this should block while we scan all the consents of the
      // interpolation and then re can return
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

  if (next_is(lexer, '\'')) {
    advance(lexer);
    return 0;
  }
  return last;
}

static bool scan_identifier(TSLexer *lexer) {
  if (iswalpha(lexer->lookahead) || next_is(lexer, '_')) {
    advance(lexer);
    while (iswalnum(lexer->lookahead) || next_is(lexer, '_') ||
           next_is(lexer, '\'')) {
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
        if (next_is(lexer, ')')) {
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
        if (next_is(lexer, '%')) {
          advance(lexer);
          if (next_is(lexer, '%')) advance(lexer);
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
    scanner->states[i] = calloc(1, sizeof(Scan_state));
  }
  return scanner;
}

static void destroy(Scanner *scanner) {
  for (size_t i = 0; i < QUOTED_STRING_DEPTH; i++) {
    free(scanner->states[i]);
  }
  free(scanner);
}
static size_t total_quoted_string_length(Scanner *scanner) {
  size_t length = 0;
  for (size_t i = 0; i < scanner->state_depth; i++) {
    Scan_state *state = scanner->states[i];
    length += state->id_length;
  }
  return length;
}

static unsigned serialize_scan_state(Scan_state *state, char *buffer) {
  buffer[0] = state->state;
  buffer[1] = state->id_length;
  memcpy(buffer + 2, state->id, state->id_length);

  return 2 + state->id_length;
}

static unsigned serialize(Scanner *scanner, char *buffer) {
  buffer[0] = scanner->state_depth;

  unsigned length = 1;
  for (size_t j = 0; j < scanner->state_depth; j++) {
    Scan_state *state = scanner->states[j];
    length += serialize_scan_state(state, buffer + length);
  }
  return length;
}
static const char *deserialize_scan_state(Scan_state *state,
                                          const char *buffer) {
  state->state = buffer[0];
  state->id_length = buffer[1];
  memcpy(state->id, buffer + 2, state->id_length);

  return buffer + state->id_length + 2;
}

// DONE
static void deserialize(Scanner *scanner, const char *buffer, unsigned length) {
  if (length > 0) {
    scanner->state_depth = buffer[0];
    const char *offset_buffer = buffer + 1;
    for (size_t j = 0; j < scanner->state_depth; j++) {
      Scan_state *state = scanner->states[j];
      offset_buffer = deserialize_scan_state(state, offset_buffer);
    }
  }
}
static inline bool try_parse_line_number_directive(Scanner *scanner,
                                                   TSLexer *lexer) {
  advance(lexer);

  while (next_is(lexer, ' ') || next_is(lexer, '\t')) {
    advance(lexer);
  }

  if (!iswdigit(lexer->lookahead)) return false;
  while (iswdigit(lexer->lookahead)) advance(lexer);

  while (next_is(lexer, ' ') || next_is(lexer, '\t')) {
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

  while (lexer->lookahead != '\n' && lexer->lookahead != '\r' && !eof(lexer)) {
    advance(lexer);
  }

  lexer->result_symbol = LINE_NUMBER_DIRECTIVE;
  return true;
}

static inline bool parse_start_string(Scanner *scanner, TSLexer *lexer) {
  advance(lexer);
  add_state(scanner, IN_STRING);
  lexer->result_symbol = STRING_DELIM;
  return true;
}

static bool scan(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols) {
  if(eof(lexer)){
    return false;
  }
  while (iswspace(lexer->lookahead)) {
    skip(lexer);
  }
  enum ParserState p_state = IN_NOTHING;

  if (scanner->state_depth != 0) {
    p_state = get_current_state(scanner)->state;
  }

  switch (p_state) {
    case IN_QUOTED_STRING:
      if (valid_symbols[LEFT_INTERPOLATION_DELIM] && next_is(lexer, '%')) {
        advance(lexer);
        lexer->result_symbol = LEFT_INTERPOLATION_DELIM;
        return scan_left_interpolation_delim(scanner, lexer);
      }
      if (valid_symbols[RIGHT_QUOTED_STRING_DELIM] && next_is(lexer, '|')) {
        advance(lexer);
        lexer->result_symbol = RIGHT_QUOTED_STRING_DELIM;
        return scan_right_quoted_string_delimiter(scanner, lexer);
      }
      break;

    case IN_STRING:

      if (valid_symbols[STRING_DELIM] && next_is(lexer, '"')) {
        advance(lexer);
        remove_state(scanner);
        lexer->result_symbol = STRING_DELIM;
        // if we might be coming to the end of the comment
        return true;
      }
      break;
    case IN_COMMENT:
      if (valid_symbols[STRING_DELIM] && next_is(lexer, '"')) {
        return parse_start_string(scanner, lexer);
      }
      if (valid_symbols[LEFT_QUOTED_STRING_DELIM] &&
          (iswlower(lexer->lookahead) || next_is(lexer, '_') ||
           next_is(lexer, '|'))) {
        lexer->result_symbol = LEFT_QUOTED_STRING_DELIM;
        return parse_left_quoted_string_delimiter(scanner, lexer);
      }
    break;
    case IN_INTERPOLATION:
      if (valid_symbols[RIGHT_INTERPOLATION_DELIM] && next_is(lexer, '}')) {
        advance(lexer);
        lexer->result_symbol = RIGHT_INTERPOLATION_DELIM;
        return scan_right_interpolation_delim(scanner, lexer);
      }
    case IN_NOTHING:

      if (valid_symbols[COMMENT_START] && next_is(lexer, '(')) {
        advance(lexer);
        if (next_is(lexer, '*')) {
          advance(lexer);
          lexer->result_symbol = COMMENT_START;
          return true;
        }
        return false;
      }
      if (valid_symbols[LEFT_QUOTED_STRING_DELIM] &&
          (iswlower(lexer->lookahead) || next_is(lexer, '_') ||
           next_is(lexer, '|'))) {
        lexer->result_symbol = LEFT_QUOTED_STRING_DELIM;
        return parse_left_quoted_string_delimiter(scanner, lexer);
      }
      if (valid_symbols[STRING_DELIM] && next_is(lexer, '"')) {
        return parse_start_string(scanner, lexer);
      }
      if (next_is(lexer, '#') && lexer->get_column(lexer) == 0) {
        return try_parse_line_number_directive(scanner, lexer);
      }
      if (valid_symbols[COMMENT_BODY]&& !(next_is(lexer,'{' )||next_is(lexer,'"' ))) {
        // TODO i should actually be able to keep advancing untill i hit either
        // " { or *
        lexer->result_symbol = COMMENT_BODY;
        // if we might be coming to the end of the comment
        if (next_is(lexer, '*')) {
          mark_end(lexer);
          advance(lexer);
          if (next_is(lexer, ')')) {
            return false;
          }
          // we need to move the end else we will ge stuck here
          mark_end(lexer);
          return true;
        } else {
          advance(lexer);
          // while (!(next_is(lexer, '*') || next_is(lexer, '"') ||
          //          next_is(lexer, '{'))) {
          //   advance(lexer);
          // }

          return true;
        }
      }
      if (valid_symbols[NULL_CHARACTER] && next_is(lexer, '\0') &&
          !eof(lexer)) {
        advance(lexer);
        lexer->result_symbol = NULL_CHARACTER;
        return true;
      }
      break;
  }

  return false;
}

#endif  // TREE_SITTER_OCAML_SCANNER_H_
