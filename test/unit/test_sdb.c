#include "minunit.h"
#include <sdb/sdb.h>
#include <fcntl.h>
#include <stdio.h>

static bool foreach_delete_cb(void *user, const char *key, const char *val) {
	if (strcmp (key, "bar")) {
		sdb_unset (user, key, 0);
	}
	return 1;
}

bool test_sdb_foreach_delete(void) {
	Sdb *db = sdb_new (NULL, NULL, false);
	sdb_set (db, "foo", "bar", 0);
	sdb_set (db, "bar", "cow", 0);
	sdb_set (db, "low", "bar", 0);
	sdb_foreach (db, foreach_delete_cb, db);

	mu_assert ("Item not deleted", !(int)(size_t)sdb_const_get (db, "foo", NULL));
	mu_assert ("Item not deleted", (int)(size_t)sdb_const_get (db, "bar", NULL));

	sdb_free (db);
	mu_end;
}

bool test_sdb_list_delete(void) {
	Sdb *db = sdb_new (NULL, NULL, false);
	sdb_set (db, "foo", "bar", 0);
	sdb_set (db, "bar", "cow", 0);
	sdb_set (db, "low", "bar", 0);
	SdbList *list = sdb_foreach_list (db, true);
	SdbListIter *iter;
	SdbKv *kv;
	ls_foreach (list, iter, kv) {
		//printf ("--> %s\n", kv->key);
		sdb_unset (db, kv->base.key, 0);
	}
	SdbList *list2 = sdb_foreach_list (db, true);
	mu_assert ("List is empty", !ls_length (list2));
	ls_free(list);
	ls_free(list2);
	sdb_free (db);
	mu_end;
}

static inline int fakerand() {
	static int i = 1;
	return i++;
}

static bool test_sdb_list_big(void) {
	char buffer[256];
	Sdb *db = sdb_new0 ();
	int i;
	for (i = 0; i < 500000; i++) {
		(void)snprintf (buffer, sizeof (buffer), "0x%x", fakerand ());
		sdb_num_set (db, buffer, i + 1, 0);
	}
	ut64 now = sdb_now ();
	SdbList *list = sdb_foreach_list (db, true);
	ut64 then = sdb_now ();
	printf ("(TIME DIFF = %d) ", (int)(then - now));
	// TODO: verify if its sorted
	ls_free(list);
	sdb_free (db);
	mu_end;
}

bool test_sdb_delete_none(void) {
	Sdb *db = sdb_new (NULL, NULL, false);
	sdb_set (db, "foo", "bar", 0);
	sdb_set (db, "bar", "cow", 0);
	sdb_set (db, "low", "bar", 0);
	sdb_unset (db, "fundas", 0);
	sdb_unset (db, "barnas", 0);
	sdb_unset (db, "bar", 0);
	sdb_unset (db, "pinuts", 0);
	SdbList *list = sdb_foreach_list (db, false);
	mu_assert_eq (ls_length (list), 2, "Unmatched rows");
	ls_free(list);
	sdb_free (db);
	mu_end;
}

bool test_sdb_delete_alot(void) {
	Sdb *db = sdb_new (NULL, NULL, false);
	char buffer[32];
	const int count = 2048;
	int i;

	for (i = 0; i < count; i++) {
		(void)snprintf (buffer, sizeof (buffer), "key.%d", i);
		sdb_set (db, buffer, "bar", 0);
	}
	for (i = 0; i < count; i++) {
		(void)snprintf (buffer, sizeof (buffer), "key.%d", i);
		sdb_unset (db, buffer, 0);
	}
	SdbList *list = sdb_foreach_list (db, false);
	mu_assert_eq (ls_length (list), 0, "Unmatched rows");
	ls_free(list);
	sdb_free (db);

	mu_end;
}

bool test_sdb_milset(void) {
	int i = 0;
	const int MAX = 19999999;
	Sdb *s = sdb_new0 ();
	sdb_set (s, "foo", "bar", 0);
	for (i = 0; i < MAX ; i++) {
		if (!sdb_set (s, "foo", "bar", 0)) {
			mu_assert ("milset: sdb_set failed", 0);
			break;
		}
	}
	sdb_free (s);
	mu_end;
}

static bool test_sdb_milset_random(void) {
	int i = 0;
	const int MAX = 19999999;
	bool solved = true;
	char buffer[256];
	Sdb *s = sdb_new0 ();
	sdb_set (s, "foo", "bar", 0);
	for (i = 0; i < MAX ; i++) {
		(void)snprintf (buffer, sizeof (buffer), "bar%d", i);
		if (!sdb_set (s, "foo", buffer, 0)) {
			solved = false;
			break;
		}
	}
	mu_assert ("milset: sdb_set", solved);
	sdb_free (s);
	mu_end;
}

bool test_sdb_namespace(void) {
	bool solved = false;
	const char *dbname = ".bar";
	Sdb *s = sdb_new0 ();
	if (!s) {
		return false;
	}
	unlink (dbname);
	Sdb *n = sdb_ns (s, dbname, 1);
	if (n) {
		sdb_set (n, "user.pancake", "pancake foo", 0);
		sdb_set (n, "user.password", "jklsdf8r3o", 0);
		/* FIXED BUG1 ns_sync doesnt creates the database file */
		sdb_ns_sync (s);
		// sdb_sync (n);
		/* FIXED BUG2 crash in free */
	 	sdb_free (s);

		int fd = open (dbname, O_RDONLY);
		if (fd != -1) {
			close (fd);
			solved = true;
		}
		unlink (dbname);
	}
	mu_assert ("namespace sync", solved);
	mu_end;
}

static bool foreach_filter_cb(void *user, const char *key, const char *val) {
	return key[0] == 'b';
}

bool test_sdb_foreach_filter(void) {
	Sdb *db = sdb_new (NULL, NULL, false);
	sdb_set (db, "foo", "bar", 0);
	sdb_set (db, "bar", "cow", 0);
	sdb_set (db, "boo", "cow", 0);
	sdb_set (db, "low", "bar", 0);
	sdb_set (db, "bip", "cow", 0);
	SdbList *ls = sdb_foreach_list_filter (db, foreach_filter_cb, true);
	SdbListIter *it = ls_iterator (ls);
	HtPPKv *kv = ls_iter_get (it);
	mu_assert_streq ((const char *)kv->key, "bar", "list should be sorted");
	kv = ls_iter_get (it);
	mu_assert_streq ((const char *)kv->key, "bip", "list should be sorted");
	kv = ls_iter_get (it);
	mu_assert_streq ((const char *)kv->key, "boo", "list should be sorted");
	mu_assert_null (it, "list should be terminated");
	ls_free (ls);
	sdb_free (db);
	mu_end;
}

bool test_sdb_copy() {
	Sdb *src = sdb_new0 ();
	sdb_set (src, "i am", "thou", 0);
	sdb_set (src, "thou art", "i", 0);
	Sdb *sub = sdb_ns (src, "subns", true);
	sdb_set (sub, "radare", "cool", 0);
	sdb_set (sub, "radare2", "cooler", 0);
	sdb_set (sub, "cutter", "coolest", 0);

	Sdb *dst = sdb_new0 ();
	sdb_copy (src, dst);
	sdb_free (src);

	mu_assert_eq (sdb_count (dst), 2, "root count");
	mu_assert_streq (sdb_const_get (dst, "i am", 0), "thou", "root entries");
	mu_assert_streq (sdb_const_get (dst, "thou art", 0), "i", "root entries");
	mu_assert_eq (ls_length (dst->ns), 1, "sub ns count");
	Sdb *dst_sub = sdb_ns (dst, "subns", false);
	mu_assert_notnull (dst_sub, "subns");
	mu_assert_eq (sdb_count (dst_sub), 3, "sub ns entries count");
	mu_assert_streq (sdb_const_get (dst_sub, "radare", 0), "cool", "sub ns entries");
	mu_assert_streq (sdb_const_get (dst_sub, "radare2", 0), "cooler", "sub ns entries");
	mu_assert_streq (sdb_const_get (dst_sub, "cutter", 0), "coolest", "sub ns entries");

	sdb_free (dst);
	mu_end;
}

#define PERTURBATOR "\\,\";]\n [\r}{'=/"

static const char *text_ref_simple =
	"/\n"
	"aaa=stuff\n"
	"bbb=other stuff\n"
	"somekey=somevalue\n"
	"\n"
	"/subnamespace\n"
	"\\/more stuff=in sub\n"
	"key in sub=value in sub\n"
	"\n"
	"/subnamespace/subsub\n"
	"some stuff=also down here\n";

// the order in here is implementation-defined
static const char *text_ref_simple_unsorted =
	"/\n"
	"aaa=stuff\n"
	"somekey=somevalue\n"
	"bbb=other stuff\n"
	"\n"
	"/subnamespace\n"
	"key in sub=value in sub\n"
	"\\/more stuff=in sub\n"
	"\n"
	"/subnamespace/subsub\n"
	"some stuff=also down here\n";

static const char *text_ref =
	"/\n"
	"\\\\,\";]\\n [\\r}{'\\=/key\\\\,\";]\\n [\\r}{'\\=/=\\\\,\";]\\n [\\r}{'=/value\\\\,\";]\\n [\\r}{'=/\n"
	"aaa=stuff\n"
	"bbb=other stuff\n"
	"\n"
	"/sub\\\\,\";]\\n [\\r}{'=\\/namespace\n"
	"\\/more stuff\\n=\\nin\\nsub\\n\n"
	"key\\\\,\";]\\n [\\r}{'\\=/in sub=value\\\\,\";]\\n [\\r}{'=/in sub\n"
	"\n"
	"/sub\\\\,\";]\\n [\\r}{'=\\/namespace/subsub\n"
	"some stuff=also down here\n";

static const char *text_ref_bad_nl =
	"/\r\n"
	"non=unix\r"
	"newlines=should\n"
	"be=banned";

static const char *text_ref_broken =
	"/////\n"
	"just garbage\n"
	"no\\=equal\\=here\n"
	"=nokey\n"
	"novalue=\n"
	"more/garbage/////\n"
	"\\/\\/\\/unnecessary=\\/escapes\\u\\x\\a\n"
	"////some/////subns/\n"
	"more=equal=signs=than=one=\n"
	"////some//subns//more\n"
	"////some//subns//more/further//////\n"
	"escape=newlines\\\n"
	"also escape=nothingness\\";

static const char *text_ref_path_last_line =
	"/\r\n"
	"some=stuff\r"
	"/a/useless/namespace";

static Sdb *text_ref_simple_db() {
	Sdb *db = sdb_new0 ();
	sdb_set (db, "somekey", "somevalue", 0);
	sdb_set (db, "aaa", "stuff", 0);
	sdb_set (db, "bbb", "other stuff", 0);

	Sdb *sub = sdb_ns (db, "subnamespace", true);
	sdb_set (sub, "key in sub", "value in sub", 0);
	sdb_set (sub, "/more stuff", "in sub", 0);

	Sdb *subsub = sdb_ns (sub, "subsub", true);
	sdb_set (subsub, "some stuff", "also down here", 0);
	return db;
}

static Sdb *text_ref_db() {
	Sdb *db = sdb_new0 ();
	sdb_set (db, PERTURBATOR"key"PERTURBATOR, PERTURBATOR"value"PERTURBATOR, 0);
	sdb_set (db, "aaa", "stuff", 0);
	sdb_set (db, "bbb", "other stuff", 0);

	Sdb *sub = sdb_ns (db, "sub"PERTURBATOR"namespace", true);
	sdb_set (sub, "key"PERTURBATOR"in sub", "value"PERTURBATOR"in sub", 0);
	sdb_set (sub, "/more stuff\n", "\nin\nsub\n", 0);

	Sdb *subsub = sdb_ns (sub, "subsub", true);
	sdb_set (subsub, "some stuff", "also down here", 0);
	return db;
}

static Sdb *text_ref_bad_nl_db() {
	Sdb *db = sdb_new0 ();
	sdb_set (db, "non", "unix", 0);
	sdb_set (db, "newlines", "should", 0);
	sdb_set (db, "be", "banned", 0);
	return db;
}

static Sdb *text_ref_broken_db() {
	Sdb *db = sdb_new0 ();
	sdb_set (db, "///unnecessary", "/escapesuxa", 0);
	Sdb *a = sdb_ns (db, "some", true);
	Sdb *b = sdb_ns (a, "subns", true);
	sdb_set (b, "more", "equal=signs=than=one=", 0);
	Sdb *c = sdb_ns (b, "more", true);
	Sdb *d = sdb_ns (c, "further", true);
	sdb_set (d, "escape", "newlines", 0);
	sdb_set (d, "also escape", "nothingness", 0);
	return db;
}

static Sdb *text_ref_path_last_line_db() {
	Sdb *db = sdb_new0 ();
	sdb_set (db, "some", "stuff", 0);
	Sdb *a = sdb_ns (db, "a", true);
	Sdb *b = sdb_ns (a, "useless", true);
	sdb_ns (b, "namespace", true);
	return db;
}

static int tmpfile_new (const char *filename, const char *buf, size_t sz) {
	int fd = open (filename, O_RDWR | O_CREAT | O_TRUNC | O_BINARY, 0644);
	if (fd < 0) {
		return -1;
	}
	if (!buf) {
		return fd;
	}
	int w = write (fd, buf, sz);
	if (w < (int)sz) {
		close (fd);
		return -1;
	}
	lseek (fd, 0, SEEK_SET);
	return fd;
}

#define TEST_BUF_SZ 0x4000

bool test_sdb_text_save_simple() {
	Sdb *db = text_ref_simple_db ();

	int fd = tmpfile_new (".text_save_simple", NULL, 0);
	bool succ = sdb_text_save_fd (db, fd, true);
	lseek (fd, 0, SEEK_SET);
	char buf[TEST_BUF_SZ] = { 0 };
	read (fd, buf, sizeof (buf) - 1);
	close (fd);
	unlink (".text_save_simple");

	sdb_free (db);

	mu_assert_true (succ, "save success");
	mu_assert_streq (buf, text_ref_simple, "text save");

	mu_end;
}

bool test_sdb_text_save_simple_unsorted() {
	Sdb *db = text_ref_simple_db ();

	int fd = tmpfile_new (".text_save_simple_unsorted", NULL, 0);
	bool succ = sdb_text_save_fd (db, fd, false);
	lseek (fd, 0, SEEK_SET);
	char buf[TEST_BUF_SZ] = { 0 };
	read (fd, buf, sizeof (buf) - 1);
	close (fd);
	unlink (".text_save_simple_unsorted");

	sdb_free (db);

	mu_assert_true (succ, "save success");
	mu_assert_streq (buf, text_ref_simple_unsorted, "text save");

	mu_end;
}

bool test_sdb_text_save() {
	Sdb *db = text_ref_db ();

	int fd = tmpfile_new (".text_save", NULL, 0);
	bool succ = sdb_text_save_fd (db, fd, true);
	lseek (fd, 0, SEEK_SET);
	char buf[TEST_BUF_SZ] = { 0 };
	read (fd, buf, sizeof (buf) - 1);
	close (fd);
	unlink (".text_save");

	sdb_free (db);

	mu_assert_true (succ, "save success");
	mu_assert_streq (buf, text_ref, "text save");

	mu_end;
}

static void diff_cb(const SdbDiff *diff, void *user) {
	char buf[2048];
	if (sdb_diff_format (buf, sizeof (buf), diff) < 0) {
		return;
	}
	printf ("%s\n", buf);
}

bool test_sdb_text_load_simple() {
	char *buf = strdup (text_ref_simple);
	Sdb *db = sdb_new0 ();
	bool succ = sdb_text_load_buf (db, buf, strlen (buf));
	free (buf);

	mu_assert_true (succ, "load success");
	Sdb *ref_db = text_ref_simple_db ();
	bool eq = sdb_diff (ref_db, db, diff_cb, NULL);
	sdb_free (ref_db);
	sdb_free (db);
	mu_assert_true (eq, "load correct");
	mu_end;
}

bool test_sdb_text_load() {
	char *buf = strdup (text_ref);
	Sdb *db = sdb_new0 ();
	bool succ = sdb_text_load_buf (db, buf, strlen (buf));
	free (buf);

	mu_assert_true (succ, "load success");
	Sdb *ref_db = text_ref_db ();
	bool eq = sdb_diff (ref_db, db, diff_cb, NULL);
	sdb_free (ref_db);
	sdb_free (db);
	mu_assert_true (eq, "load correct");
	mu_end;
}

bool test_sdb_text_load_bad_nl() {
	char *buf = strdup (text_ref_bad_nl);
	Sdb *db = sdb_new0 ();
	bool succ = sdb_text_load_buf (db, buf, strlen (buf));
	free (buf);

	mu_assert_true (succ, "load success");
	Sdb *ref_db = text_ref_bad_nl_db ();
	bool eq = sdb_diff (ref_db, db, diff_cb, NULL);
	sdb_free (ref_db);
	sdb_free (db);
	mu_assert_true (eq, "load correct");
	mu_end;
}

bool test_sdb_text_load_broken() {
	char *buf = strdup (text_ref_broken);
	Sdb *db = sdb_new0 ();
	bool succ = sdb_text_load_buf (db, buf, strlen (buf));
	free (buf);

	mu_assert_true (succ, "load success");
	Sdb *ref_db = text_ref_broken_db ();
	bool eq = sdb_diff (ref_db, db, diff_cb, NULL);
	sdb_free (ref_db);
	sdb_free (db);
	mu_assert_true (eq, "load correct");
	mu_end;
}

bool test_sdb_text_load_path_last_line() {
	char *buf = strdup (text_ref_path_last_line);
	Sdb *db = sdb_new0 ();
	bool succ = sdb_text_load_buf (db, buf, strlen (buf));
	free (buf);

	mu_assert_true (succ, "load success");
	Sdb *ref_db = text_ref_path_last_line_db ();
	bool eq = sdb_diff (ref_db, db, diff_cb, NULL);
	sdb_free (ref_db);
	sdb_free (db);
	mu_assert_true (eq, "load correct");
	mu_end;
}

bool test_sdb_text_load_file() {
	close (tmpfile_new (".text_load_simple", text_ref_simple, strlen (text_ref_simple)));
	Sdb *db = sdb_new0 ();
	bool succ = sdb_text_load (db, ".text_load_simple");
	unlink (".text_load_simple");

	mu_assert_true (succ, "load success");
	Sdb *ref_db = text_ref_simple_db ();
	bool eq = sdb_diff (ref_db, db, diff_cb, NULL);
	sdb_free (ref_db);
	sdb_free (db);
	mu_assert_true (eq, "load correct");
	mu_end;
}

int all_tests() {
	// XXX two bugs found with crash
	mu_run_test (test_sdb_namespace);
	mu_run_test (test_sdb_foreach_delete);
	mu_run_test (test_sdb_list_delete);
	mu_run_test (test_sdb_delete_none);
	mu_run_test (test_sdb_delete_alot);
	mu_run_test (test_sdb_milset);
	mu_run_test (test_sdb_milset_random);
	mu_run_test (test_sdb_list_big);
	mu_run_test (test_sdb_foreach_filter);
	mu_run_test (test_sdb_copy);
	mu_run_test (test_sdb_text_save_simple);
	mu_run_test (test_sdb_text_save_simple_unsorted);
	mu_run_test (test_sdb_text_load_simple);
	mu_run_test (test_sdb_text_save);
	mu_run_test (test_sdb_text_load);
	mu_run_test (test_sdb_text_load_bad_nl);
	mu_run_test (test_sdb_text_load_broken);
	mu_run_test (test_sdb_text_load_path_last_line);
	mu_run_test (test_sdb_text_load_file);
	return tests_passed != tests_run;
}

int main(int argc, char **argv) {
	return all_tests ();
}
