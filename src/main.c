# include <signal.h>
#include <fcntl.h>
#ifndef HAVE_SYSTEM
#define HAVE_SYSTEM 1
#endif
#include <sdb/sdb.h>
#if USE_DLSYSTEM
#include <dlfcn.h>
#endif

#if __SDB_WINDOWS__
#define ftruncate _chsize
#endif

// TODO: enums must be uppercase
typedef enum {
	text,
	zero,
	json,
	cgen,
	diff,
	perf,
	sdb_gen
} MainFormat;

// TODO: enums must be uppercase
typedef enum {
	nope = 0,
	dash,
	eqeq,
	dobl
} MainCreate;

typedef struct {
	int argc;
	const char **argv;
	int argi;
	int db0;
	bool failed;
	const char *db;
	const char *outfile;
	const char *db2;
	const char *grep;
	ut32 options;
	bool textmode;
	MainCreate create;
	MainFormat format;
} MainOptions;

static bool save = false;
static Sdb *s = NULL;
static ut32 options = SDB_OPTION_FS | SDB_OPTION_NOSTAMP;

static void terminate(int sig UNUSED) {
	if (!s) {
		return;
	}
	if (save && !sdb_sync (s)) {
		sdb_free (s);
		s = NULL;
		exit (1);
	}
	sdb_gh_fini ();
	// sdb_free (s);
	exit (sig < 2? sig: 0);
}

static int write_null(void) {
	// success = write returns 1 == 1 -> 0
	// failure = write returns 0 != 1 -> 1
	return write (1, "", 1) != 1;
}

#define BS 128
#define USE_SLURPIN 1

static char *slurp(FILE *f, size_t *sz) {
	int blocksize = BS;
	static int bufsize = BS;
	static char *next = NULL;
	static size_t nextlen = 0;
	size_t len, rr, rr2;
	char *tmp, *buf = NULL;
	if (sz) {
		*sz = 0;
	}
#if USE_SLURPIN
	if (!sz) {
		/* this is faster but have limits */
		/* run test/add10k.sh script to benchmark */
		const int buf_size = 96096;

		buf = (char *)sdb_gh_calloc (1, buf_size);
		if (!buf) {
			return NULL;
		}

		if (!fgets (buf, buf_size, f)) {
			sdb_gh_free (buf);
			return NULL;
		}
		if (feof (f)) {
			sdb_gh_free (buf);
			return NULL;
		}

		size_t buf_len = strlen (buf);
		if (buf_len > 0) {
			buf[buf_len - 1] = '\0';
		}

		char *newbuf = (char *)sdb_gh_realloc (buf, buf_len + 1);
		return newbuf? newbuf: buf;
	}
#endif
	buf = (char *)sdb_gh_calloc (BS + 1, 1);
	if (!buf) {
		return NULL;
	}

	len = 0;
	for (;;) {
		if (next) {
			sdb_gh_free (buf);
			buf = next;
			bufsize = nextlen + blocksize;
			//len = nextlen;
			rr = nextlen;
			rr2 = fread (buf + nextlen, 1, blocksize, f);
			if (rr2 > 0) {
				rr += rr2;
				bufsize += rr2;
			}
			next = NULL;
			nextlen = 0;
		} else {
			rr = fread (buf + len, 1, blocksize, f);
		}
		if (rr < 1) { // EOF
			buf[len] = 0;
			next = NULL;
			break;
		}
		len += rr;
		//buf[len] = 0;
#if !USE_SLURPIN
		if (!sz) {
			char *nl = strchr (buf, '\n');
			if (nl) {
				*nl++ = 0;
				int nlen = nl - buf;
				nextlen = len - nlen;
				if (nextlen > 0) {
					next = (char *)sdb_gh_malloc (nextlen + blocksize + 1);
					if (!next) {
						eprintf ("Cannot malloc %d\n", nextlen);
						break;
					}
					memcpy (next, nl, nextlen);
					if (!*next) {
						next = NULL;
					} else {
					//	continue;
					}
				} else {
					next = NULL;
					nextlen = 0; //strlen (next);;
				}
				break;
			}
		}
#endif
		bufsize += blocksize;
		tmp = (char *)sdb_gh_realloc (buf, bufsize + 1);
		if (!tmp) {
			bufsize -= blocksize;
			break;
		}
		memset (tmp + bufsize - blocksize, 0, blocksize);
		buf = tmp;
	}
	if (sz) {
		*sz = len;
	}
	if (len < 1) {
		sdb_gh_free (buf);
		return buf = NULL;
	}
	buf[len] = 0;
	return buf;
}

#if USE_MMAN
static void synchronize(int sig UNUSED) {
	// TODO: must be in sdb_sync() or wat?
	sdb_sync (s);
	Sdb *n = sdb_new (s->path, s->name, s->lock);
	if (n) {
		sdb_config (n, options);
		sdb_free (s);
		s = n;
	}
}
#endif

static char* get_name(const char*name) {
	if (!name || !*name) {
		return NULL;
	}
	const char *l = name + strlen (name) - 1;
	while (*l && l > name) {
		if (*l == '/') {
			name = l + 1;
			break;
		}
		l--;
	}
	char *n = sdb_strdup (name);
	char *v, *d = n;
	for (v = (char*)n; *v; v++) {
		if (*v == '.') {
			break;
		}
		*d++ = *v;
	}
	*d++ = 0;
	return n;
}

static char* get_cname(const char*name) {
	if (!name || !*name) {
		return NULL;
	}
	const char *l = name + strlen (name) - 1;
	while (*l && l > name) {
		if (*l == '/') {
			name = l + 1;
			break;
		}
		l--;
	}
	char *n = sdb_strdup (name);
	char *v, *d = n;
	for (v = (char*)n; *v; v++) {
		if (*v == '/' || *v == '-') {
			*d++ = '_';
			continue;
		}
		if (*v == '.') {
			break;
		}
		*d++ = *v;
	}
	*d++ = 0;
	return n;
}

static char *escape(const char *b, int ch) {
	char *a = (char *)sdb_gh_calloc ((1 + strlen (b)), 4);
	if (!a) {
		return NULL;
	}
	char *c = a;
	while (*b) {
		if (*b == ch) {
			*c = '_';
		} else
		switch (*b) {
		case '"':
			*c++ = '\\';
			*c++ = '"';
			break;
		case '\\':
			*c++ = '\\';
			*c++ = '\\';
			break;
		case '\r':
			*c++ = '\\';
			*c++ = 'r';
			break;
		case '\n':
			*c++ = '\\';
			*c++ = 'n';
			break;
		case '\t':
			*c++ = '\\';
			*c++ = 't';
			break;
#if 0
		case ' ':
			*c++ = '\\';
			*c++ = 's';
			break;
#endif
		default:
			*c = *b;
			break;
		}
		b++;
		c++;
	}
	return a;
}

static void sdb_dump_cb(MainOptions *mo, const char *k, const char *v, const char *comma) {
	switch (mo->format) {
	case json:
		if (!strcmp (v, "true") || !strcmp (v, "false")) {
			printf ("%s\"%s\":%s", comma, k, v);
		} else if (sdb_isnum (v)) {
			printf ("%s\"%s\":%" PRIu64, comma, k, sdb_atoi (v));
		} else if (*v == '{' || *v == '[') {
			printf ("%s\"%s\":%s", comma, k, v);
		} else {
			printf ("%s\"%s\":\"%s\"", comma, k, v);
		}
		break;
	case perf:
	case cgen:
		{
			char *a = escape (k, ',');
			char *b = escape (v, 0);
			if (mo->textmode) {
				printf ("  {\"%s\",\"%s\"}, \n", a, b);
			} else {
				printf ("%s,\"%s\"\n", a, b);
			}
			sdb_gh_free (a);
			sdb_gh_free (b);
		}
		break;
	case zero:
		printf ("%s=%s", k, v);
		break;
	default:
		printf ("%s=%s\n", k, v);
		break;
	}
}

static int sdb_dump(MainOptions *mo) {
	const char *dbname = mo->db;
	const char *expgrep = mo->grep;
	const bool grep = mo->grep;

	char *v = NULL;
	char k[SDB_MAX_KEY] = { 0 };
	const char *comma = "";
	Sdb *db = sdb_new (NULL, dbname, 0);
	if (!db) {
		return 1;
	}
	char *cname = get_cname (dbname);
	char *name = get_name (dbname);
	sdb_config (db, options);
	sdb_dump_begin (db);
	switch (mo->format) {
	case cgen:
	case perf:
		{
		char *s = sdb_cgen_header (cname, mo->textmode);
		printf ("%s", s);
		free (s);
		}
		break;
	case json:
		printf ("{");
		break;
	default:
		break;
	}

	int ret = 0;
	if (db->fd == -1) {
		SdbList *l = sdb_foreach_list (db, true);
		if (!mo->textmode && mo->format == cgen && ls_length (l) > SDB_MAX_GPERF_KEYS) {
			ls_free (l);
			eprintf ("Error: gperf doesn't work with datasets with more than 15.000 keys.\n");
			ret = -1;
			goto fail;
		}
		SdbKv *kv;
		SdbListIter *it;
		ls_foreach_cast (l, it, SdbKv*, kv) {
			const char *sk = sdbkv_key (kv);
			const char *sv = sdbkv_value (kv);
			if (sv && *sv && grep && !strstr (sk, expgrep) && !strstr (sv, expgrep)) {
				continue;
			}
			sdb_dump_cb (mo, sk, sv, comma);
			comma = ",";
		}
		ls_free (l);
	} else {
		int count = 0;
		while (sdb_dump_dupnext (db, k, &v, NULL)) {
			if (v && *v && grep && !strstr (k, expgrep) && !strstr (v, expgrep)) {
				sdb_gh_free (v);
				continue;
			}
			sdb_dump_cb (mo, k, v, comma);
			comma = ",";
			sdb_gh_free (v);
			if (!mo->textmode && mo->format == cgen && count++ > SDB_MAX_GPERF_KEYS) {
				eprintf ("Error: gperf doesn't work with datasets with more than 15.000 keys.\n");
				ret = -1;
			}
		}
	}
	if (ret == 0) {
		switch (mo->format) {
		case zero:
			fflush (stdout);
			ret = write_null ();
			break;
		case perf:
		case cgen:
			{
				char *footer = sdb_cgen_footer (name, cname, mo->textmode);
				printf ("%s\n", footer);
				free (footer);
			}
			break;
		case json:
			printf ("}\n");
			break;
		default:
			break;
		}
	}
fail:
	sdb_gh_free (cname);
	sdb_gh_free (name);
	sdb_free (db);
	return ret;
}

static int insertkeys(Sdb *db, const char **args, int nargs, int mode) {
	int must_save = 0;
	if (args && nargs > 0) {
		int i;
		for (i = 0; i < nargs; i++) {
			switch (mode) {
			case '-':
				if (sdb_query (db, args[i])) {
					must_save = true;
				}
				break;
			case '=':
				if (strchr (args[i], '=')) {
					char *v, *kv = (char *) sdb_strdup (args[i]);
					v = strchr (kv, '=');
					if (v) {
						*v++ = 0;
						sdb_disk_insert (db, kv, v);
					}
					sdb_gh_free (kv);
				}
				break;
			}
		}
	}
	return must_save;
}

static int createdb(const char *f, const char **args, int nargs) {
	s = sdb_new (NULL, f, 0);
	if (!s) {
		eprintf ("Cannot create database\n");
		return 1;
	}
	sdb_config (s, options);
	int ret = 0;
	if (args) {
		int i;
		for (i = 0; i < nargs; i++) {
			if (!sdb_text_load (s, args[i])) {
				eprintf ("Failed to load text sdb from %s\n", args[i]);
			}
		}
	} else {
		size_t len;
		char *in = slurp (stdin, &len);
		if (!in) {
			return 0;
		}
		if (!sdb_text_load_buf (s, in, len)) {
			eprintf ("Failed to read text sdb from stdin\n");
		}
		sdb_gh_free (in);
	}
	sdb_sync (s);
	return ret;
}

static int showusage(int o) {
	printf ("usage: sdb [-0cCdDehjJrtv|-D A B] [-|db] "
		"[.file]|[-=]|==||[-+][(idx)key[:json|=value] ..]\n");
	if (o == 2) {
		printf ("  -0      terminate results with \\x00\n"
			"  -c      count the number of keys database\n"
			"  -C      create foo.{c,h} for embedding (uses gperf)\n"
			"  -d      decode base64 from stdin\n"
			"  -D      diff two databases\n"
			"  -e      encode stdin as base64\n"
			"  -g [..] grep expression\n"
			"  -G      print database in gperf format\n"
			"  -h      show this help\n"
			"  -j      output in json\n"
			"  -o [f]  output file name for -C -t\n"
			"  -J      enable journaling\n"
			"  -r      process .sdb.txt files in the given path\n"
			"  -t      use textmode (for -C)\n"
			"  -v      show version information\n");
		return 0;
	}
	return o;
}

static int showversion(void) {
	printf ("sdb " SDB_VERSION "\n");
	fflush (stdout);
	return 0;
}

static int jsonIndent(void) {
	size_t len;
	char *out;
	char *in = slurp (stdin, &len);
	if (!in) {
		return 0;
	}
	out = sdb_json_indent (in, "  ");
	if (!out) {
		sdb_gh_free (in);
		return 1;
	}
	puts (out);
	sdb_gh_free (out);
	sdb_gh_free (in);
	return 0;
}

static int base64encode(void) {
	char *out;
	size_t len = 0;
	ut8 *in = (ut8 *) slurp (stdin, &len);
	if (!in) {
		return 0;
	}
	out = sdb_encode (in, (int)len);
	if (!out) {
		sdb_gh_free (in);
		return 1;
	}
	puts (out);
	sdb_gh_free (out);
	sdb_gh_free (in);
	return 0;
}

static int base64decode(void) {
	ut8 *out;
	size_t len, ret = 1;
	char *in = slurp (stdin, &len);
	if (in) {
		int declen;
		out = sdb_decode (in, &declen);
		if (out && declen >= 0) {
			ret = (write (1, out, declen) == declen)? 0: 1;
		}
		sdb_gh_free (out);
		sdb_gh_free (in);
	}
	return ret;
}

static void dbdiff_cb(const SdbDiff *diff, void *user) {
	char sbuf[512];
	int r = sdb_diff_format (sbuf, sizeof(sbuf), diff);
	if (r < 0) {
		return;
	}
	char *buf = sbuf;
	char *hbuf = NULL;
	if ((size_t)r >= sizeof (sbuf)) {
		hbuf = (char *)sdb_gh_malloc (r + 1);
		if (!hbuf) {
			return;
		}
		r = sdb_diff_format (hbuf, r + 1, diff);
		if (r < 0) {
			goto beach;
		}
	}
	printf ("\x1b[%sm%s\x1b[0m\n", diff->add ? "32" : "31", buf);
beach:
	sdb_gh_free (hbuf);
}

static bool dbdiff(const char *a, const char *b) {
	Sdb *A = sdb_new (NULL, a, 0);
	Sdb *B = sdb_new (NULL, b, 0);
	bool equal = sdb_diff (A, B, dbdiff_cb, NULL);
	sdb_free (A);
	sdb_free (B);
	return equal;
}

static int showcount(const char *db) {
	ut32 d;
	s = sdb_new (NULL, db, 0);
	if (sdb_stats (s, &d, NULL)) {
		printf ("%d\n", d);
	}
	// TODO: show version, timestamp information
	sdb_free (s);
	return 0;
}

static int sdb_system(const char *cmd) {
	static int (*sys)(const char *cmd) = NULL;
	if (!sys) {
#if HAVE_SYSTEM
#if USE_DLSYSTEM
		sys = dlsym (NULL, "system");
		if (!sys) {
			sys = puts;
			return -1;
		}
#else
		sys = system;
#endif
#endif
	}
	return sys? sys (cmd): -1;
}

static int gen_gperf(MainOptions *mo, const char *file, const char *name) {
	const size_t buf_size = 4096;
	char *buf = (char *)sdb_gh_malloc (buf_size);
	if (!buf) {
		return -1;
	}
	size_t out_size = strlen (file) + 32;
	char *out = (char *)sdb_gh_malloc (out_size);
	if (!out) {
		sdb_gh_free (buf);
		return -1;
	}
	if (mo->outfile) {
		snprintf (out, out_size, "%s", mo->outfile);
	} else {
		snprintf (out, out_size, "%s.%s", name, (mo->textmode)? "c": "gperf");
	}
	int wd = open (out, O_RDWR, 0644);
	if (wd == -1) {
		wd = open (out, O_RDWR | O_CREAT, 0644);
	} else {
		if (ftruncate (wd, 0) == -1) {
			sdb_gh_free (out);
			sdb_gh_free (buf);
			close (wd);
			return -1;
		}
	}
	int rc = -1;
	if (wd != -1) {
#if __wasi__ || EMSCRIPTEN
		rc = sdb_dump (mo); // file, MODE_CGEN, false, NULL);
		fflush (stdout);
#else
		dup2 (1, 999);
		dup2 (wd, 1);
		rc = sdb_dump (mo); // file, MODE_CGEN, false, NULL);
		fflush (stdout);
		close (wd);
		dup2 (999, 1);
#endif
	} else {
		eprintf ("Cannot create .%s\n", out);
	}
	if (mo->textmode) {
		// dont do much
	} else {
		if (rc == 0) {
			char *cname = get_cname (name);
			if (!cname) {
				sdb_gh_free (out);
				sdb_gh_free (buf);
				return -1;
			}
			snprintf (buf, buf_size, "gperf -aclEDCIG --null-strings -H sdb_hash_c_%s"
					" -N sdb_get_c_%s -t %s.gperf > %s.c\n", cname, cname, name, name);
			sdb_gh_free (cname);
			rc = sdb_system (buf);
			if (rc == 0) {
				snprintf (buf, buf_size, "gcc -DMAIN=1 %s.c ; ./a.out > %s.h\n", name, name);
				rc = sdb_system (buf);
				if (rc == 0) {
					eprintf ("Generated %s.c and %s.h\n", name, name);
				}
			} else {
				eprintf ("Cannot run gperf: %s\n", buf);
			}
		} else {
			eprintf ("Outdated sdb binary in PATH?\n");
		}
	}
	sdb_gh_free (out);
	sdb_gh_free (buf);
	return rc;
}

static const char *main_argparse_getarg(MainOptions *mo) {
	int cur = mo->argi;
	if (mo->argi + 1 >= mo->argc) {
		return NULL;
	}
	mo->argi++;
	mo->db0++;
	return mo->argv[cur];
}

static bool main_argparse_flag(MainOptions *mo, char flag) {
	mo->argi++;
	switch (flag) {
	case '0':
		mo->format = zero;
		break;
	case 'h':
		return showusage (2);
	case 'o':
		mo->outfile = main_argparse_getarg (mo);
		break;
	case 'v':
		return showversion ();
	case 'e':
		return base64encode ();
	case 'd':
		return base64decode ();
	case 'r':
		if (mo->format == sdb_gen) {
			mo->options |= (1 << 16); // Use a bit in options as a flag to track if -r was seen
		}
		mo->format = sdb_gen;
		break;
	case 'j':
		mo->format = json;
		if (mo->argi >= mo->argc) {
			return jsonIndent ();
		}
		break;
	case 'c':
		if (mo->argc <= 3) {
			return showusage (1);
		} else {
			const char *db = main_argparse_getarg (mo);
			if (!db) {
				return showusage (1);
			}
			return showcount (db);
		}
		break;
	case 'g':
		mo->grep = main_argparse_getarg (mo);
		if (!mo->grep) {
			eprintf ("Missing argument for -g\n");
			return false;
		}
		break;
	case 'D':
		if (mo->argi + 1 >=  mo->argc) {
			return showusage (0);
		}
		mo->format = diff;
		break;
	case 'G':
		mo->format = perf;
		break;
	case 'C':
		mo->format = cgen;
		break;
	case 't':
		mo->textmode = true;
		break;
	case 'J':
		mo->options |= SDB_OPTION_JOURNAL;
		// expect argument
		break;
	default:
		return false;
	}
	return true;
}

static MainOptions *main_argparse(MainOptions *mo, int argc, const char **argv) {
	if (!mo) {
		return NULL;
	}
	mo->argc = argc;
	mo->argv = argv;
	mo->options = SDB_OPTION_FS | SDB_OPTION_NOSTAMP;
	mo->failed = true;
	int i;
	mo->argi = 1;
	for (i = 1; i < argc; i++) {
		mo->db0++;
		i = mo->argi;
		if (argv[i][0] == '-' && argv[i][1]) {
			int j = 1;
			while (argv[i][j]) {
				if (!main_argparse_flag (mo, argv[i][j])) {
					mo->db = argv[mo->argi];
					mo->db0 = i + 1;
					// invalid flag
					break; // return NULL;
				}
				if (i != mo->argi) {
					break;
				}
				j++;
			}
		} else {
			mo->db = argv[mo->db0];
			if (i + 1 < argc) {
				switch (argv[i + 1][0]) {
				case '-':
					if (!argv[i + 1][1]) {
						mo->create = dash;
					}
					break;
				case '=':
					if (argv[i + 1][1]) {
						mo->create = dobl;
					} else {
						mo->create = eqeq;
					}
					break;
				default:
					mo->db2 = argv[i];
					break;
				}
			}
			break;
		}
	}
	// mo->db = mo->argv[mo->db0 + 1];
	mo->db = argv[mo->db0];
	return mo;
}

SDB_API int sdb_main(int argc, const char **argv) {
	char *line;
	int i;

	/* terminate flags */
	if (argc < 2) {
		return showusage (1);
	}
	MainOptions _mo = {0};
	MainOptions *mo = &_mo;
	main_argparse (mo, argc, argv);
	// -j json return sdb_dump (argv[db0 + 1], MODE_JSON);
	// -G sdb_dump (argv[db0 + 1], MODE_CGEN); // gperf
	// -C print C/H files
	// -t text
	switch (mo->format) {
	case diff:
		if (mo->db && mo->db2) {
			return dbdiff (mo->db, mo->db2)? 0: 1;
		}
		return showusage (1);
	case perf:
		return sdb_dump (mo);
	case sdb_gen:
		{
			if (mo->db0 >= argc) {
				return showusage (1);
			}
			// Enable mirror mode if -r is used more than once
			const bool mirror_mode = mo->options & (1<<16);
			return sdb_tool (mo->argv[mo->db0], mirror_mode)? 0: 1;
		}
		break;
	case cgen:
		{
			if (mo->db0 >= argc) {
				return showusage (1);
			}
			const char *file = mo->argv[mo->db0];
			char *name = sdb_strdup (file);
			char *p = strchr (name, '.');
			if (p) {
				*p = 0;
			}
			int rc = gen_gperf (mo, file, name);
			sdb_gh_free (name);
			return rc;
		}
		break;
	case text:
	default:
		break;
	}

	if (!mo->db && !mo->create) {
		return showusage (1);
	}
	if (!mo->create && mo->db) {
		if (!strcmp (mo->db, "-")) {
			mo->create = dash;
			mo->db = NULL;
			mo->argi--;
			mo->db0 = mo->argi;
		}
	}
#if USE_MMAN
	signal (SIGINT, terminate);
	signal (SIGHUP, synchronize);
#endif
	int ret = 0;
	switch (mo->create) {
	case dash: // "-"
		if ((s = sdb_new (NULL, mo->db, 0))) {
			sdb_config (s, options);
			int kvs = mo->db0 + 2;
			if (mo->argi + 2 < mo->argc) {
				for (i = mo->argi + 2; i < argc; i++) {
					if (sdb_query (s, mo->argv[i])) {
						save = true;
					}
					if (mo->format) {
						fflush (stdout);
						ret = write_null ();
					}
				}
			} else {
				if (kvs < argc && insertkeys (s, argv + mo->argi + 2, argc - kvs, '-')) {
					save = true;
				}
				for (; (line = slurp (stdin, NULL));) {
					if (sdb_query (s, line)) {
						save = true;
					}
					if (mo->format) {
						fflush (stdout);
						ret = write_null ();
					}
					sdb_gh_free (line);
				}
			}
		} else {
			eprintf ("Cannot create database\n");
		}
		break;
	case eqeq: // "="
		ret = createdb (mo->db, NULL, 0);
		break;
	case dobl: // "=="
		{
			int delta = mo->db0 + 2;
			ret = createdb (mo->db, mo->argv + delta, mo->argc - delta);
		}
		break;
	default:
		// "sdb test.db"
		s = sdb_new (NULL, mo->db, 0);
		if (!s) {
			return 1;
		}
		sdb_config (s, options);
		if (mo->argi + 1 < mo->argc) {
			for (i = mo->db0 + 1; i < argc; i++) {
				if (sdb_query (s, mo->argv[i])) {
					save = true;
				}
				if (mo->format) {
					fflush (stdout);
					ret = write_null ();
				}
			}
		} else {
			return sdb_dump (mo);
		}
		break;
	}
	terminate (ret);
	return ret;
}
