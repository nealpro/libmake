#include "libmake.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

static void fail(const char *msg)
{
	fprintf(stderr, "FAIL: %s\n", msg);
	failures++;
}

static void assert_true(int cond, const char *msg)
{
	if (!cond)
		fail(msg);
}

static void assert_eq_str(const char *actual, const char *expected,
			  const char *msg)
{
	if (strcmp(actual, expected) == 0)
		return;

	fprintf(stderr, "FAIL: %s\nexpected:\n%s\nactual:\n%s\n", msg, expected,
		actual);
	failures++;
}

static void write_file(const char *path, const char *contents)
{
	FILE *file = fopen(path, "w");
	if (!file) {
		fprintf(stderr, "FAIL: fopen(%s): %s\n", path, strerror(errno));
		exit(1);
	}
	fputs(contents, file);
	fclose(file);
}

static char *read_stream(FILE *file)
{
	long len;
	char *buf;

	fflush(file);
	len = ftell(file);
	if (len < 0) {
		perror("ftell");
		exit(1);
	}
	rewind(file);

	buf = calloc((size_t)len + 1, 1);
	if (!buf) {
		perror("calloc");
		exit(1);
	}
	if (fread(buf, 1, (size_t)len, file) != (size_t)len) {
		perror("fread");
		exit(1);
	}
	return buf;
}

static char *dump_graph(lmk_t *lmk)
{
	FILE *out = tmpfile();
	char *buf;

	if (!out) {
		perror("tmpfile");
		exit(1);
	}
	lmk_dump_graph_json(lmk, out);
	buf = read_stream(out);
	fclose(out);
	return buf;
}

static int dump_makefile(lmk_t *lmk, char **makefile)
{
	FILE *out = tmpfile();
	int ret;

	if (!out) {
		perror("tmpfile");
		exit(1);
	}
	ret = lmk_dump_makefile(lmk, out);
	*makefile = read_stream(out);
	fclose(out);
	return ret;
}

static int explain_build(lmk_t *lmk, const char *target, char **json)
{
	FILE *out = tmpfile();
	int ret;

	if (!out) {
		perror("tmpfile");
		exit(1);
	}
	ret = lmk_explain_build(lmk, target, out);
	*json = read_stream(out);
	fclose(out);
	return ret;
}

static void test_dump_graph_json(void)
{
	const char *target = "lib\"x\\y\nz\t";
	const char *expected =
		"{\n"
		"  \"nodes\": [\n"
		"    {\"name\": \"all\", \"commands\": [], \"deps\": "
		"[\"lib\\\"x\\\\y\\nz\\t\"]},\n"
		"    {\"name\": \"lib\\\"x\\\\y\\nz\\t\", \"commands\": "
		"[\"printf \\\"hi\\\\n\\\"\"], \"deps\": [\"dep\"]},\n"
		"    {\"name\": \"dep\", \"commands\": [], \"deps\": []}\n"
		"  ]\n"
		"}\n";
	lmk_t *lmk = lmk_create();
	char *json;

	lmk_rule(lmk, "all", &target, 1, NULL, 0);
	lmk_rule(lmk, target, (const char *[]){"dep"}, 1,
		 (const char *[]){"printf \"hi\\n\""}, 1);

	json = dump_graph(lmk);
	assert_eq_str(json, expected, "graph JSON should be deterministic");

	free(json);
	lmk_free(lmk);
}

static void test_dump_makefile(void)
{
	const char *expected = "all: out\n"
			       "\n"
			       "out: in\n"
			       "\tcp in out\n";
	lmk_t *lmk = lmk_create();
	char *makefile;

	assert_true(lmk_rule(lmk, "all", (const char *[]){"out"}, 1, NULL, 0) ==
			    0,
		    "all rule should be added");
	assert_true(lmk_rule(lmk, "out", (const char *[]){"in"}, 1,
			     (const char *[]){"cp in out"}, 1) == 0,
		    "out rule should be added");
	assert_true(dump_makefile(lmk, &makefile) == 0,
		    "Makefile dump should succeed");
	assert_eq_str(makefile, expected, "Makefile dump should be stable");

	free(makefile);
	lmk_free(lmk);
}

static void test_dump_makefile_rejects_unsupported_names(void)
{
	lmk_t *bad_colon = lmk_create();
	lmk_t *bad_space = lmk_create();
	char *makefile;

	assert_true(lmk_rule(bad_colon, "bad:target", NULL, 0, NULL, 0) == 0,
		    "unsupported target should still be accepted in graph");
	assert_true(dump_makefile(bad_colon, &makefile) != 0,
		    "Makefile dump should reject colon in target");
	assert_eq_str(makefile, "",
		      "failed Makefile dump should not emit partial content");
	free(makefile);

	assert_true(lmk_rule(bad_space, "out", (const char *[]){"my input"}, 1,
			     (const char *[]){"cp 'my input' out"}, 1) == 0,
		    "space dependency should still be accepted in graph");
	assert_true(dump_makefile(bad_space, &makefile) != 0,
		    "Makefile dump should reject internal whitespace");
	assert_eq_str(makefile, "",
		      "failed Makefile dump should not emit partial content");

	free(makefile);
	lmk_free(bad_colon);
	lmk_free(bad_space);
}

static void test_makefile_round_trip(void)
{
	const char *expected =
		"{\n"
		"  \"nodes\": [\n"
		"    {\"name\": \"all\", \"commands\": [], \"deps\": "
		"[\"out\"]},\n"
		"    {\"name\": \"out\", \"commands\": [\"cp in out\"], "
		"\"deps\": [\"in\"]},\n"
		"    {\"name\": \"in\", \"commands\": [], \"deps\": []}\n"
		"  ]\n"
		"}\n";
	lmk_t *source = lmk_create();
	lmk_t *loaded = lmk_create();
	char *makefile;
	char *json;

	assert_true(lmk_rule(source, "all", (const char *[]){"out"}, 1, NULL,
			     0) == 0,
		    "source all rule should be added");
	assert_true(lmk_rule(source, "out", (const char *[]){"in"}, 1,
			     (const char *[]){"cp in out"}, 1) == 0,
		    "source out rule should be added");
	assert_true(lmk_rule(source, "in", NULL, 0, NULL, 0) == 0,
		    "source input rule should be added");

	assert_true(dump_makefile(source, &makefile) == 0,
		    "source Makefile dump should succeed");
	write_file("roundtrip.mk", makefile);
	free(makefile);

	assert_true(lmk_load_makefile(loaded, "roundtrip.mk") == 0,
		    "loading dumped Makefile should succeed");
	json = dump_graph(loaded);
	assert_eq_str(json, expected, "round-tripped graph should match");

	free(json);
	lmk_free(source);
	lmk_free(loaded);
}

static void test_explain_build(void)
{
	const char *expected_missing =
		"{\n"
		"  \"target\": \"out\",\n"
		"  \"plan\": [\n"
		"    {\"node\": \"out\", \"action\": \"rebuild\", "
		"\"reason\": \"file_missing\"}\n"
		"  ],\n"
		"  \"skipped\": [\n"
		"  ]\n"
		"}\n";
	const char *expected_up_to_date =
		"{\n"
		"  \"target\": \"out\",\n"
		"  \"plan\": [\n"
		"  ],\n"
		"  \"skipped\": [\n"
		"    {\"node\": \"out\", \"reason\": \"up_to_date\"}\n"
		"  ]\n"
		"}\n";
	const char *expected_dependency_newer =
		"{\n"
		"  \"target\": \"out\",\n"
		"  \"plan\": [\n"
		"    {\"node\": \"out\", \"action\": \"rebuild\", "
		"\"reason\": \"dependency_newer\", \"dep\": \"in\"}\n"
		"  ],\n"
		"  \"skipped\": [\n"
		"  ]\n"
		"}\n";
	lmk_t *lmk = lmk_create();
	char *json;

	lmk_rule(lmk, "out", (const char *[]){"in"}, 1,
		 (const char *[]){"cp in out"}, 1);
	lmk_rule(lmk, "in", NULL, 0, NULL, 0);

	assert_true(explain_build(lmk, "out", &json) == 0,
		    "explain missing output should succeed");
	assert_eq_str(json, expected_missing,
		      "explain should report missing target");
	free(json);

	write_file("in", "v1\n");
	assert_true(lmk_build(lmk, "out") == 0, "initial build should succeed");
	lmk_reset(lmk);

	assert_true(explain_build(lmk, "out", &json) == 0,
		    "explain up-to-date output should succeed");
	assert_eq_str(json, expected_up_to_date,
		      "explain should report up-to-date target");
	free(json);

	sleep(1);
	write_file("in", "v2\n");
	assert_true(explain_build(lmk, "out", &json) == 0,
		    "explain stale output should succeed");
	assert_eq_str(json, expected_dependency_newer,
		      "explain should report newer dependency");
	free(json);

	lmk_free(lmk);
}

static void test_missing_and_cycles(void)
{
	lmk_t *missing = lmk_create();
	lmk_t *build_cycle = lmk_create();
	lmk_t *explain_cycle = lmk_create();
	FILE *missing_out = tmpfile();
	char *json;

	assert_true(lmk_build(missing, "nope") != 0,
		    "build should fail for missing target");
	if (!missing_out) {
		perror("tmpfile");
		exit(1);
	}
	assert_true(lmk_explain_build(missing, "nope", missing_out) != 0,
		    "explain should fail for missing target");
	fclose(missing_out);

	lmk_rule(build_cycle, "a", (const char *[]){"b"}, 1,
		 (const char *[]){"touch a"}, 1);
	lmk_rule(build_cycle, "b", (const char *[]){"a"}, 1,
		 (const char *[]){"touch b"}, 1);
	assert_true(lmk_build(build_cycle, "a") != 0,
		    "build should fail for circular dependency");

	lmk_rule(explain_cycle, "a", (const char *[]){"b"}, 1,
		 (const char *[]){"touch a"}, 1);
	lmk_rule(explain_cycle, "b", (const char *[]){"a"}, 1,
		 (const char *[]){"touch b"}, 1);
	assert_true(explain_build(explain_cycle, "a", &json) != 0,
		    "explain should fail for circular dependency");
	assert_eq_str(json, "", "cycle explain should not write JSON plan");
	free(json);

	lmk_free(missing);
	lmk_free(build_cycle);
	lmk_free(explain_cycle);
}

static void test_reset_and_shared_dependency(void)
{
	lmk_t *lmk = lmk_create();
	char *json;

	lmk_rule(lmk, "all", (const char *[]){"left", "right"}, 2, NULL, 0);
	lmk_rule(lmk, "left", (const char *[]){"input"}, 1,
		 (const char *[]){"cp input left"}, 1);
	lmk_rule(lmk, "right", (const char *[]){"input"}, 1,
		 (const char *[]){"cp input right"}, 1);
	lmk_rule(lmk, "input", NULL, 0, NULL, 0);

	write_file("input", "v1\n");
	assert_true(lmk_build(lmk, "all") == 0,
		    "shared dependency build should succeed");

	lmk_reset(lmk);
	assert_true(explain_build(lmk, "all", &json) == 0,
		    "shared dependency explain should succeed");
	assert_true(strstr(json, "\"node\": \"left\"") != NULL,
		    "explain should mention left");
	assert_true(strstr(json, "\"node\": \"right\"") != NULL,
		    "explain should mention right");
	free(json);

	sleep(1);
	write_file("input", "v2\n");
	lmk_reset(lmk);
	assert_true(lmk_build(lmk, "all") == 0,
		    "reset graph should allow second build");

	lmk_free(lmk);
}

int main(int argc, char **argv)
{
	const char *workdir;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <workdir>\n", argv[0]);
		return 2;
	}
	workdir = argv[1];
	if (mkdir(workdir, 0777) != 0 && errno != EEXIST) {
		perror("mkdir");
		return 2;
	}
	if (chdir(workdir) != 0) {
		perror("chdir");
		return 2;
	}

	test_dump_graph_json();
	test_dump_makefile();
	test_dump_makefile_rejects_unsupported_names();
	test_makefile_round_trip();
	test_explain_build();
	test_missing_and_cycles();
	test_reset_and_shared_dependency();

	if (failures)
		return 1;

	puts("PASS: libmake API checks succeeded");
	return 0;
}
