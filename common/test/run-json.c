#include "../json.c"
#include <stdio.h>

/* AUTOGENERATED MOCKS START */
/* AUTOGENERATED MOCKS END */


// issue #577

static void do_json_tok_btcnano_amount(const char* val, uint64_t expected)
{
	uint64_t amount;
	jsmntok_t tok;

	tok.start = 0;
	tok.end = strlen(val);

	fprintf(stderr, "do_json_tok_btcnano_amount(\"%s\", %"PRIu64"): ", val, expected);

	assert(json_tok_btcnano_amount(val, &tok, &amount) == true);
	assert(amount == expected);

	fprintf(stderr, "ok\n");
}


static int test_json_tok_btcnano_amount(void)
{
	do_json_tok_btcnano_amount("0.00000001", 1);
	do_json_tok_btcnano_amount("0.00000007", 7);
	do_json_tok_btcnano_amount("0.00000008", 8);
	do_json_tok_btcnano_amount("0.00000010", 10);
	do_json_tok_btcnano_amount("0.12345678", 12345678);
	do_json_tok_btcnano_amount("0.01234567", 1234567);
	do_json_tok_btcnano_amount("123.45678900", 12345678900);

	return 0;
}

static int test_json_filter(void)
{
	struct json_result *result = new_json_result(NULL);
	jsmntok_t *toks;
	const jsmntok_t *x;
	bool valid;
	int i;
	char *badstr = tal_arr(result, char, 256);
	const char *str;

	/* Fill with junk, and nul-terminate (256 -> 0) */
	for (i = 1; i < 257; i++)
		badstr[i-1] = i;

	json_object_start(result, NULL);
	json_add_string(result, "x", badstr);
	json_object_end(result);

	/* Parse back in, make sure nothing crazy. */
	str = json_result_string(result);

	toks = json_parse_input(str, strlen(str), &valid);
	assert(valid);
	assert(toks);

	assert(toks[0].type == JSMN_OBJECT);
	x = json_get_member(str, toks, "x");
	assert(x);
	assert(x->type == JSMN_STRING);
	assert((x->end - x->start) == 255);
	for (i = x->start; i < x->end; i++) {
		assert(cisprint(str[i]));
		assert(str[i] != '\\');
		assert(str[i] != '"');
		assert(str[i] == '?' || str[i] == badstr[i - x->start]);
	}
	tal_free(result);
	return 0;
}

static void test_json_escape(void)
{
	int i;
	const char *str;

	for (i = 1; i < 256; i++) {
		char badstr[2];
		struct json_result *result = new_json_result(NULL);

		badstr[0] = i;
		badstr[1] = 0;

		json_object_start(result, NULL);
		json_add_string_escape(result, "x", badstr);
		json_object_end(result);

		str = json_result_string(result);
		if (i == '\\' || i == '"'
		    || i == '\n' || i == '\r' || i == '\b'
		    || i == '\t' || i == '\f')
			assert(strstarts(str, "{ \"x\" : \"\\"));
		else if (i < 32 || i == 127)
			assert(strstarts(str, "{ \"x\" : \"\\u00"));
		else {
			char expect[] = "{ \"x\" : \"?\" }";
			expect[9] = i;
			assert(streq(str, expect));
		}
		tal_free(result);
	}
}

int main(void)
{
	test_json_tok_btcnano_amount();
	test_json_filter();
	test_json_escape();
}
