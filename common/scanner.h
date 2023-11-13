#ifndef TREE_SITTER_OCAML_SCANNER_H_
#define TREE_SITTER_OCAML_SCANNER_H_

#define QUOTED_STRING_DEPTH 10

#include <assert.h>
#include <string.h>
#include <tree_sitter/parser.h>
#include <wctype.h>

enum TokenType {
  LEFT_QUOTED_STRING_DELIM,
  RIGHT_QUOTED_STRING_DELIM,
  START_INTERPOLATION,
  LINE_NUMBER_DIRECTIVE,
  NULL_CHARACTER,
};

enum ParserState {
  IN_QUOTED_STRING,
  IN_NOTHING,
};

typedef struct {
  size_t id_length;
  enum ParserState state;
  char id[1000];
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


static bool scan_left_interpolation_delim(Scanner *scanner, TSLexer *lexer) {
  //because we only want to know if we are starting the interpolation we mark this as the end 
  mark_end(lexer);
  // scan the Module name if it exists
  if (iswupper(lexer->lookahead)) {
    advance(lexer);
    while (iswalpha(lexer->lookahead)) {
      advance(lexer);
    }
  }
  if (lexer->lookahead != '{') return false;
  advance(lexer);
  return true;
}

static bool scan(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols) {
  while (iswspace(lexer->lookahead)) {
    skip(lexer);
  }
  enum ParserState p_state = IN_NOTHING;

  if (scanner->state_depth != 0) {
    p_state = get_current_state(scanner)->state;
  }

  switch (p_state) {
    case IN_QUOTED_STRING:
      if (valid_symbols[RIGHT_QUOTED_STRING_DELIM] && next_is(lexer, '|')) {
        advance(lexer);
        lexer->result_symbol = RIGHT_QUOTED_STRING_DELIM;
        return scan_right_quoted_string_delimiter(scanner, lexer);
      }

      if (valid_symbols[START_INTERPOLATION] && next_is(lexer, '$')) {
        advance(lexer);
        lexer->result_symbol = START_INTERPOLATION;
        return scan_left_interpolation_delim(scanner, lexer);
      }
    case IN_NOTHING:
      if (valid_symbols[LEFT_QUOTED_STRING_DELIM] &&
          (iswlower(lexer->lookahead) || next_is(lexer, '_') ||
           next_is(lexer, '|'))) {
        lexer->result_symbol = LEFT_QUOTED_STRING_DELIM;
        return parse_left_quoted_string_delimiter(scanner, lexer);
      }
      if (next_is(lexer, '#') && lexer->get_column(lexer) == 0) {
        return try_parse_line_number_directive(scanner, lexer);
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
