/*
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2,
 * as published by the Free Software Foundation.
 *
 * In addition to the permissions in the GNU General Public License,
 * the authors give you unlimited permission to link the compiled
 * version of this file into combinations with other programs,
 * and to distribute those combinations without any restriction
 * coming from the use of this file.  (The General Public License
 * restrictions do apply in other respects; for example, they cover
 * modification of the file, and distribution when not linked into
 * a combined executable.)
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "common.h"
#include "fileops.h"
#include "hashtable.h"
#include "config.h"

struct git_config {
	char *file_path;

	struct {
		gitfo_buf buffer;
		char *read_ptr;
		int line_number;
		int eof;
	} reader;

	git_hashtable *vars;
};

typedef enum {
	GIT_VAR_INT,
	GIT_VAR_BOOL,
	GIT_VAR_STR
} git_config_type;

struct git_config_var {
	git_config_type type;
	char *name;
	union {
		unsigned char boolean;
		long integer;
		char *string;
	} value;
};

typedef struct git_config git_config;


uint32_t config_table_hash(const void *key)
{
	const char *var_name = (char *)key;
	return git__hash(key, strlen(var_name), 0x5273eae3);
}

int config_table_haskey(void *object, const void *key)
{
	git_config_var *var = (git_config_var *)object;
	const char *var_name = (const char *)key;

	return (strcmp(var->name, var_name) == 0);
}

int git_config_open(git_config **cfg_out, const char *path)
{
	git_config *cfg;

	assert(cfg_out && path);

	cfg = git__malloc(sizeof(git_config));
	if (cfg == NULL)
		return GIT_ENOMEM;

	memset(cfg, 0x0, sizeof(git_config));

	cfg->file_path = git__strdup(path);
	if (cfg->file_path == NULL)
		return GIT_ENOMEM;

	cfg->vars = git_hashtable_alloc(16, config_table_hash, config_table_haskey);
	if (cfg->vars == NULL)
		return GIT_ENOMEM;

	*cfg_out = cfg;
	return GIT_SUCCESS;
}

void git_config_free(git_config *cfg)
{
	if (cfg == NULL)
		return;

	free(cfg->file_path);
	git_hashtable_free(cfg->vars);
	gitfo_free_buf(&cfg->reader.buffer);

	free(cfg);
}

static int cfg_getchar_raw(git_config *cfg)
{
	int c;

	c = *cfg->reader.read_ptr++;

	/*
	Win 32 line breaks: if we find a \r\n sequence,
	return only the \n as a newline
	*/
	if (c == '\r' && *cfg->reader.read_ptr == '\n') {
		cfg->reader.read_ptr++;
		c = '\n';
	}

	if (c == '\n')
		cfg->reader.line_number++;

	if (c == 0) {
		cfg->reader.eof = 1;
		c = '\n';
	}

	return c;
}

#define SKIP_WHITESPACE (1 << 1)
#define SKIP_COMMENTS (1 << 2)

static int cfg_getchar(git_config *cfg_file, int flags)
{
	const int skip_whitespace = (flags & SKIP_WHITESPACE);
	const int skip_comments = (flags & SKIP_COMMENTS);
	int c;

	assert(cfg_file->reader.read_ptr);

	do c = cfg_getchar_raw(cfg_file);
	while (skip_whitespace && isspace(c));

	if (skip_comments && (c == '#' || c == ';')) {
    	do c = cfg_getchar_raw(cfg_file);
    	while (c != '\n');
	}

	return c;
}

static const char *LINEBREAK_UNIX = "\\\n";
static const char *LINEBREAK_WIN32 = "\\\r\n";

static int is_linebreak(const char *pos)
{
	return	memcmp(pos - 1, LINEBREAK_UNIX, sizeof(LINEBREAK_UNIX)) == 0 ||
			memcmp(pos - 2, LINEBREAK_WIN32, sizeof(LINEBREAK_WIN32)) == 0;
}

static char *cfg_readline(git_config *cfg)
{
	char *line = NULL;
	char *line_src, *line_end;
	int line_len;

	line_src = cfg->reader.read_ptr;
    line_end = strchr(line_src, '\n');

	while (is_linebreak(line_end))
		line_end = strchr(line_end + 1, '\n');

    /* no newline at EOF */
	if (line_end == NULL)
		line_end = strchr(line_src, 0);

	while (line_src < line_end && isspace(*line_src))
		line_src++;

	line = (char *)git__malloc((size_t)(line_end - line_src) + 1);
	if (line == NULL)
		return NULL;

	line_len = 0;
	while (line_src < line_end) {

		if (memcmp(line_src, LINEBREAK_UNIX, sizeof(LINEBREAK_UNIX)) == 0) {
			line_src += sizeof(LINEBREAK_UNIX);
			continue;
		}

		if (memcmp(line_src, LINEBREAK_WIN32, sizeof(LINEBREAK_WIN32)) == 0) {
			line_src += sizeof(LINEBREAK_WIN32);
			continue;
		}

		line[line_len++] = *line_src++;
	}

	line[line_len] = '\0';

	while (--line_len >= 0 && isspace(line[line_len]))
		line[line_len] = '\0';

	if (*line_end == '\n')
		line_end++;

	if (*line_end == '\0')
		cfg->reader.eof = 1;

	cfg->reader.line_number++;
	cfg->reader.read_ptr = line_end;

	return line;
}

static inline int config_keychar(int c)
{
	return isalnum(c) || c == '-';
}

static char *parse_section_header_ext(char *base_name, git_config *cfg)
{
	return base_name;
}

static int parse_section_header(char **section_out, const char *line)
{
	char *name, *name_start, *name_end;
	int name_length, c;

	/* find the end of the variable's name */
	name_end = strchr(name_start, ']');
	if (name_end == NULL)
		return NULL;

	name = (char *)git__malloc((size_t)(name_end - name_start) + 1);
	if (name == NULL)
		return NULL;

	name_length = 0;
	c = cfg_getchar(cfg, SKIP_WHITESPACE | SKIP_COMMENTS);

	do {
		if (cfg->reader.eof)
			goto error;

		if (isspace(c))
			return parse_section_name_ext(name, cfg);

		if (!config_keychar(c) && c != '.')
			goto error;

		name[name_length++] = tolower(c);

	} while ((c = cfg_getchar(cfg, SKIP_COMMENTS)) != ']');
	
	name[name_length] = 0;
	return name;

error:
	free(name);
	return NULL;
}

static int skip_bom(git_config *cfg)
{
	static const unsigned char *utf8_bom = "\xef\xbb\xbf";

	if (memcmp(cfg->reader.read_ptr, utf8_bom, sizeof(utf8_bom)) == 0)
		cfg->reader.read_ptr += sizeof(utf8_bom);

	/*  TODO: the reference implementation does pretty stupid
		shit with the BoM
	*/

	return GIT_SUCCESS;
}

/*
	(* basic types *)
	digit = "0".."9"
	integer = digit { digit }
	alphabet = "a".."z" + "A" .. "Z"

	section_char = alphabet | "." | "-"
	extension_char = (* any character except newline *)
	any_char = (* any character *)
	variable_char = "alphabet" | "-"


	(* actual grammar *)
	config = { section }

	section = header { definition }

	header = "[" section [subsection | subsection_ext] "]"

	subsection = "." section
	subsection_ext = "\"" extension "\""

	section = section_char { section_char }
	extension = extension_char { extension_char }

	definition = variable_name ["=" variable_value] "\n"

	variable_name = variable_char { variable_char }
	variable_value = string | boolean | integer

	string = quoted_string | plain_string
	quoted_string = "\"" plain_string "\""
	plain_string = { any_char }

	boolean = boolean_true | boolean_false
	boolean_true = "yes" | "1" | "true" | "on"
	boolean_false = "no" | "0" | "false" | "off"
*/

static void strip_comments(char *line)
{
	int quote_count = 0;
	char *ptr;

	for (ptr = line; *ptr; ++ptr) {
		if (ptr[0] == '"' && ptr > line && ptr[-1] != '\\')
			quote_count++;

		if ((ptr[0] == ';' || ptr[0] == '#') && (quote_count % 2) == 0) {
			ptr[0] = '\0';
			break;
		}
	}

	if (isspace(ptr[-1])) {
		/* TODO skip whitespace */
	}
}

static int config_parse(git_config *cfg_file)
{
	int error = GIT_SUCCESS;
	char *current_section = NULL;

	skip_bom(cfg_file);

	while (error == GIT_SUCCESS && !cfg_file->reader.eof) {

		char *line = cfg_readline(cfg_file);

		/* not enough memory to allocate line */
		if (line == NULL)
			return GIT_ENOMEM;

		strip_comments(line);

		switch (line[0]) {
		case '\0': /* empty line (only whitespace) */
			break;

		case '[': /* section header, new section begins */
			error = parse_section_header(&current_section, line);
			break;

		default: /* assume variable declaration */
			error = parse_variable(cfg_file, current_section, line);
			break;
		}

		free(line);
	}
	
	return error;
}

static int parse_variable(git_config *cfg, const char *section_name, const char *line)
{
	int error;
	int has_value = 1;

	const char *var_end = NULL;
	const char *value_start = NULL;

	var_end = strchr(line, '=');

	if (var_end == NULL)
		var_end = strchr(line, '\0');
	else
		value_start = var_end + 1;

	if (isspace(var_end[-1])) {
		do var_end--;
		while (isspace(var_end[0]));
	}

	if (value_start != NULL) {

		while (isspace(value_start[0]))
			value_start++;

		if (value_start[0] == '\0')
			goto error;
	}

	return GIT_SUCCESS;

error:
	return GIT_EOBJCORRUPTED;
}