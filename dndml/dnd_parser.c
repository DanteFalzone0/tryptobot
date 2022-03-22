#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "dnd_input_reader.h"
#include "dnd_lexer.h"
#include "dnd_charsheet.h"
#include "dnd_parser.h"
#include "../dice.h"

static void add_token_to_vec(token_vec_t *dest, token_t token) {
  dest->token_count++;
  dest->tokens = realloc(
    dest->tokens,
    dest->token_count * sizeof(token_t)
  );
  dest->tokens[dest->token_count - 1] = token;
};

/* This function is assigned in construct_parser() to
   the parser_t.consume() method. */
static enum parser_err parser_consume(
  parser_t *this,
  enum token_type type
) {
  if (this == NULL || this->token_vec.tokens == NULL)
    return null_ptr_error;
  if (this->token_vec.tokens[this->tok_i].type == type) {
    this->tok_i++;
    return ok;
  }
  return parser_syntax_error;
}

// Prints output to last_parser_err.txt and stderr
static inline void err_message(
  enum parser_err err,
  const char *expected_object
) {
  FILE *f = fopen("last_parser_err.txt", "w+");
  switch (err) {
    case parser_syntax_error:
      fprintf(stderr, "Syntax error: %s expected\n", expected_object);
      fprintf(f, "Syntax error: %s expected", expected_object);
    break;
    case null_ptr_error:
      fprintf(stderr, "Fatal error: unexpected null pointer\n");
      fprintf(f, "Fatal error: unexpected null pointer");
    break;
    case ok:
      fprintf(stderr, "Error: `err_message()` was called on `ok`\n");
      fprintf(f, "Error: `err_message()` was called on `ok`");
    break;
    default:
      fprintf(stderr, "Error: unknown error code `%d`\n", err);
      fprintf(f, "Error: unknown error code `%d`", err);
    break;
  }
  fclose(f);
}

// mandatory forward declarations
static section_t parse_section(parser_t *);
static field_t parse_field(parser_t *);
static stat_t parse_stat_val(parser_t *);
static char *parse_string_val(parser_t *);
static int parse_int_val(parser_t *);
static diceroll_t parse_dice_val(parser_t *);
static deathsave_t parse_deathsave_val(parser_t *);
static item_t parse_item_val(parser_t *);
static itemlist_t parse_itemlist_val(parser_t *);

/* This function is assigned in construct_parser() to
   the parser_t.parse() method. */
static charsheet_t *parser_parse(parser_t *this) {
  charsheet_t *result = malloc(sizeof(charsheet_t));
  *result = (charsheet_t){
    .filename = this->src_filename,
    .sections = NULL,
    .section_count = 0
  };
  while (this->token_vec.tokens[this->tok_i].type != eof) {
    result->section_count++;
    result->sections = realloc(
      result->sections,
      result->section_count * sizeof(section_t)
    );
    result->sections[result->section_count - 1] = parse_section(this);
    if (result->sections[result->section_count - 1].identifier == NULL) {
      free_charsheet(result);
      result = NULL;
      return result;
    }
  }
  enum parser_err err = this->consume(this, eof);
  if (err) {
    free_charsheet(result);
    err_message(err, "end of file");
    result = NULL;
    return result;
  }
  return result;
}

// must have `enum parser_err err`, `parser_t *this`, and `result` in scope
#define CONSUME_ONE_CHAR(err, type, ptr_to_free_if_err, err_str) \
  err = this->consume(this, type);                               \
  if (err) {                                                     \
    err_message(err, err_str);                                   \
    free(ptr_to_free_if_err);                                    \
    return result;                                               \
  }

/* The field `.identifier` of this function's return value
   will be NULL in case of error. */
static section_t parse_section(parser_t *this) {
  enum parser_err err;
  section_t result = {
    .identifier = NULL,
    .fields = NULL,
    .field_count = 0
  };

  err = this->consume(this, section);
  if (err) {
    err_message(err, "@section");
    return result;
  }

  if (this->token_vec.tokens[this->tok_i].type == identifier) {
    result.identifier = strndup(
      this->token_vec.tokens[this->tok_i].src_text
      + this->token_vec.tokens[this->tok_i].start,
      this->token_vec.tokens[this->tok_i].end
      - this->token_vec.tokens[this->tok_i].start
    );
    this->consume(this, identifier);
  } else {
    err_message(parser_syntax_error, "section identifier");
    return result;
  }

  CONSUME_ONE_CHAR(err, colon, result.identifier, "':'");

  while (this->token_vec.tokens[this->tok_i].type != end_section) {
    if (this->token_vec.tokens[this->tok_i].type == eof) {
      err_message(parser_syntax_error, "@end-section");
      free(result.fields);
      free(result.identifier);
      result.identifier = NULL;
      return result;
    }
    field_t curr_field = parse_field(this);
    if (curr_field.type == syntax_error) {
      //print_token(this->token_vec.tokens[this->tok_i]);
      //printf("%s\n", curr_field.string_val);
      err_message(parser_syntax_error, "@field");
      exit(1);
      //free(result.fields);
      //free(result.identifier);
      //result.identifier = NULL;
      //return result;
    }
    result.field_count++;
    result.fields = realloc(
      result.fields,
      result.field_count * sizeof(field_t)
    );
    result.fields[result.field_count - 1] = curr_field;
    CONSUME_ONE_CHAR(err, semicolon, result.fields, "';'");
  }

  this->consume(this, end_section);
  return result;
}

static field_t parse_field(parser_t *this) {
  enum parser_err err;
  field_t result = {
    .int_val = INT_MIN,
    .identifier = NULL,
    .type = syntax_error
  };

  err = this->consume(this, field);
  if (err) {
    err_message(err, "@field");
    return result;
  }

  if (this->token_vec.tokens[this->tok_i].type == identifier) {
    result.identifier = strndup(
      this->token_vec.tokens[this->tok_i].src_text
      + this->token_vec.tokens[this->tok_i].start,
      this->token_vec.tokens[this->tok_i].end
      - this->token_vec.tokens[this->tok_i].start
    );
    printf("identifier address: %p\n", result.identifier);
    printf("identifier: %s\n", result.identifier);
    this->consume(this, identifier);
  } else {
    err_message(parser_syntax_error, "field identifier");
    return result;
  }

  CONSUME_ONE_CHAR(err, colon, result.identifier, "':'");

  result.type = this->token_vec.tokens[this->tok_i].type;
  printf("from parse_field(): ");
  print_token(this->token_vec.tokens[this->tok_i]);
  printf("\ntype: %d\n", result.type);
  switch (result.type) {
    case stat_val:
      result.stat_val = parse_stat_val(this);
    break;
    case string_val:
      printf("\ntried to parse string\n");
      result.string_val = parse_string_val(this);
    break;
    case int_val:
      result.int_val = parse_int_val(this);
    break;
    case dice_val:
      result.dice_val = parse_dice_val(this);
    break;
    case deathsave_val:
      result.deathsave_val = parse_deathsave_val(this);
    break;
    case itemlist_val:
      result.itemlist_val = parse_itemlist_val(this);
    break;
    case item_val:
      result.item_val = parse_item_val(this);
    break;
    default:
      err_message(parser_syntax_error, "field value");
      free(result.identifier);
      result.type = syntax_error;
      return result;
  }

  err = this->consume(this, semicolon);
  if (err) {
    err_message(err, "';'");
    free(result.identifier);
    result.identifier = NULL;
    if (result.type == string_val) {
      free(result.string_val);
    } else if (result.type == itemlist_val) {
      free(result.itemlist_val.items);
    }
    return result;
  }

  return result;
}

#define CONSUME_INT_OR_NULL(out_int_ptr)                             \
  if (this->token_vec.tokens[this->tok_i].type == int_literal) {     \
    sscanf(                                                          \
      this->token_vec.tokens[this->tok_i].src_text +                 \
      this->token_vec.tokens[this->tok_i].start,                     \
      "%d",                                                          \
      out_int_ptr                                                    \
    );                                                               \
    this->consume(this, int_literal);                                \
  } else if (this->token_vec.tokens[this->tok_i].type != null_val) { \
    this->consume(this, null_val);                                   \
  } else {                                                           \
    err_message(parser_syntax_error, "integer or NULL");             \
    return result;                                                   \
  }

#define CONSUME_RESERVED_IDENTIFIER(err, identifier_str) \
  if (strncmp(                                    \
    this->token_vec.tokens[this->tok_i].src_text  \
    + this->token_vec.tokens[this->tok_i].start,  \
    identifier_str,                               \
    this->token_vec.tokens[this->tok_i].end       \
    - this->token_vec.tokens[this->tok_i].start   \
  )) {                                            \
    err = parser_syntax_error;                    \
    err_message(err, identifier_str);           \
    return result;                                \
  } else {                                        \
    err = this->consume(this, identifier);        \
  }

static stat_t parse_stat_val(parser_t *this) {
  enum parser_err err;
  stat_t result = { .ability = INT_MIN, .mod = INT_MIN };

  this->consume(this, stat_val);

  CONSUME_ONE_CHAR(err, open_sqr_bracket, NULL, "'['");

  CONSUME_RESERVED_IDENTIFIER(err, "ability");
  if (err) {
    err_message(err, "\"ability\"");
    return result;
  }

  CONSUME_ONE_CHAR(err, colon, NULL, "':'");

  CONSUME_INT_OR_NULL(&result.ability);

  CONSUME_ONE_CHAR(err, semicolon, NULL, "';'");

  CONSUME_RESERVED_IDENTIFIER(err, "mod");
  if (err) {
    err_message(err, "\"mod\"");
    return result;
  }

  CONSUME_ONE_CHAR(err, colon, NULL, "':'");

  CONSUME_INT_OR_NULL(&result.mod);

  CONSUME_ONE_CHAR(err, close_sqr_bracket, NULL, "']'");

  return result;
}

static char *parse_string_val(parser_t *this) {
  enum parser_err err;
  char *result = NULL;

  this->consume(this, string_val);

  CONSUME_ONE_CHAR(err, open_sqr_bracket, NULL, "'['");

  if (this->token_vec.tokens[this->tok_i].type == string_val) {
    // The token will contain quotation marks at the beginning and end,
    // so we allocate _fewer_ chars than what the token points to.
    size_t bufsize = this->token_vec.tokens[this->tok_i].end -
                     this->token_vec.tokens[this->tok_i].start - 1;
    result = malloc(bufsize);
    for (int i = 0; i < bufsize - 1; i++) {
      result[i] = (
        this->token_vec.tokens[this->tok_i].src_text +
        this->token_vec.tokens[this->tok_i].start + 1
      )[i];
    }
    result[bufsize - 1] = '\0';
  }

  CONSUME_ONE_CHAR(err, close_sqr_bracket, NULL, "']'");

  return result;
}

static int parse_int_val(parser_t *this) {
  enum parser_err err;
  int result = INT_MIN;

  this->consume(this, int_val);

  CONSUME_ONE_CHAR(err, open_sqr_bracket, NULL, "'['");

  CONSUME_INT_OR_NULL(&result);

  CONSUME_ONE_CHAR(err, close_sqr_bracket, NULL, "']'");

  return result;
}

static diceroll_t parse_dice_val(parser_t *this) {
  enum parser_err err;
  diceroll_t result = {
    .dice_ct = INT_MIN,
    .faces = INT_MIN,
    .modifier = INT_MIN,
    .value = INT_MIN
  };

  this->consume(this, dice_val);

  CONSUME_ONE_CHAR(err, open_sqr_bracket, NULL, "'['");

  CONSUME_INT_OR_NULL(&result.dice_ct);

  CONSUME_RESERVED_IDENTIFIER(err, "d");
  if (err) {
    err_message(err, "'d'");
    return result;
  }

  CONSUME_INT_OR_NULL(&result.faces);

  CONSUME_ONE_CHAR(err, plus_sign, NULL, "'+'");

  CONSUME_INT_OR_NULL(&result.modifier);

  CONSUME_ONE_CHAR(err, close_sqr_bracket, NULL, "']'");

  result.value = 0;
  return result;
}

static deathsave_t parse_deathsave_val(parser_t *this) {
  enum parser_err err;
  deathsave_t result = { .succ = INT_MIN, .fail = INT_MIN };

  this->consume(this, deathsave_val);

  CONSUME_ONE_CHAR(err, open_sqr_bracket, NULL, "'['");

  CONSUME_RESERVED_IDENTIFIER(err, "succ");
  if (err) {
    err_message(err, "\"succ\"");
    return result;
  }

  CONSUME_ONE_CHAR(err, colon, NULL, "':'");

  CONSUME_INT_OR_NULL(&result.succ);

  CONSUME_ONE_CHAR(err, semicolon, NULL, "';'");

  CONSUME_RESERVED_IDENTIFIER(err, "fail");
  if (err) {
    err_message(err, "\"fail\"");
    return result;
  }

  CONSUME_ONE_CHAR(err, colon, NULL, "':'");

  CONSUME_INT_OR_NULL(&result.fail);

  CONSUME_ONE_CHAR(err, close_sqr_bracket, NULL, "']'");

  return result;
}

static item_t parse_item_val(parser_t *this) {
  enum parser_err err;
  item_t result = {
    .val = NULL,
    .qty = INT_MIN,
    .weight = INT_MIN
  };

  this->consume(this, item_val);

  CONSUME_ONE_CHAR(err, open_sqr_bracket, NULL, "'['");

  CONSUME_RESERVED_IDENTIFIER(err, "val");
  if (err) {
    err_message(err, "\"val\"");
    return result;
  }

  CONSUME_ONE_CHAR(err, colon, NULL, "':'");

  if (this->token_vec.tokens[this->tok_i].type == string_val) {
    size_t bufsize = this->token_vec.tokens[this->tok_i].end -
                     this->token_vec.tokens[this->tok_i].start - 1;
    result.val = malloc(bufsize);
    for (int i = 0; i < bufsize - 1; i++) {
      result.val[i] = (
        this->token_vec.tokens[this->tok_i].src_text +
        this->token_vec.tokens[this->tok_i].start + 1
      )[i];
    }
    result.val[bufsize - 1] = '\0';
  }

  CONSUME_ONE_CHAR(err, semicolon, result.val, "';'");

  CONSUME_RESERVED_IDENTIFIER(err, "qty");
  if (err) {
    err_message(err, "\"qty\"");
    free(result.val);
    result.val = NULL;
    return result;
  }

  CONSUME_ONE_CHAR(err, colon, result.val, "':'");

  CONSUME_INT_OR_NULL(&result.qty);

  CONSUME_ONE_CHAR(err, semicolon, result.val, "';'");

  CONSUME_RESERVED_IDENTIFIER(err, "weight");
  if (err) {
    err_message(err, "\"weight\"");
    free(result.val);
    result.val = NULL;
    return result;
  }

  CONSUME_ONE_CHAR(err, colon, result.val, "':'");

  CONSUME_INT_OR_NULL(&result.weight);

  CONSUME_ONE_CHAR(err, close_sqr_bracket, result.val, "']'");

  return result;
}

static itemlist_t parse_itemlist_val(parser_t *this) {
  enum parser_err err;
  itemlist_t result = {
    .items = NULL,
    .item_count = 0
  };

  this->consume(this, itemlist_val);

  CONSUME_ONE_CHAR(err, open_sqr_bracket, NULL, "'['");

  while (this->token_vec.tokens[this->tok_i].type != close_sqr_bracket) {
    if (this->token_vec.tokens[this->tok_i].type == eof) {
      err_message(parser_syntax_error, "']'");
      free(result.items);
      result.item_count = 0;
      return result;
    }
    item_t item = parse_item_val(this);
    result.item_count++;
    result.items = realloc(
      result.items, result.item_count * sizeof(item_t)
    );
    result.items[result.item_count - 1] = item;
    CONSUME_ONE_CHAR(err, semicolon, NULL, "';'");
  }

  this->consume(this, close_sqr_bracket);
  return result;
}

void construct_parser(parser_t *dest, lexer_t *lex, char *src_filename) {
  dest->lexer = lex;
  dest->src_filename = src_filename;
  dest->consume = &parser_consume;
  dest->parse = &parser_parse;

  dest->token_vec.tokens = NULL;
  dest->token_vec.token_count = 0;
  token_t current_token;
  do {
    current_token = dest->lexer->get_next_token(dest->lexer);
    if (current_token.type == syntax_error) {
      fprintf(
        stderr,
        "Syntax error in token stream generated while parsing.\n"
      );
      dest->token_vec.tokens = NULL;
      dest->token_vec.tokens = 0;
      break;
    }
    add_token_to_vec(&dest->token_vec, current_token);
  } while (current_token.type != eof);
  dest->tok_i = 0;
}
