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
#include "test_lib.h"
#include "test_helpers.h"

#include "tree.h"

static const char *tree_oid = "1810dff58d8a660512d4832e740f692884338ccd";

#if 0
static int print_tree(git_repository *repo, const git_oid *tree_oid, int depth)
{
	static const char *indent = "                              ";
	git_tree *tree;
	unsigned int i;

	if (git_tree_lookup(&tree, repo, tree_oid) < GIT_SUCCESS)
		return GIT_ERROR;

	for (i = 0; i < git_tree_entrycount(tree); ++i) {
		const git_tree_entry *entry = git_tree_entry_byindex(tree, i);
		char entry_oid[40];

		git_oid_fmt(entry_oid, &entry->oid);
		printf("%.*s%o [%.*s] %s\n", depth*2, indent, entry->attr, 40, entry_oid, entry->filename);

		if (entry->attr == S_IFDIR) {
			if (print_tree(repo, &entry->oid, depth + 1) < GIT_SUCCESS) {
				git_tree_close(tree);
				return GIT_ERROR;
			}
		}
	}

	git_tree_close(tree);
	return GIT_SUCCESS;
}
#endif

BEGIN_TEST(read0, "acces randomly the entries on a loaded tree")
	git_oid id;
	git_repository *repo;
	git_tree *tree;

	must_pass(git_repository_open(&repo, REPOSITORY_FOLDER));

	git_oid_mkstr(&id, tree_oid);

	must_pass(git_tree_lookup(&tree, repo, &id));

	must_be_true(git_tree_entry_byname(tree, "README") != NULL);
	must_be_true(git_tree_entry_byname(tree, "NOTEXISTS") == NULL);
	must_be_true(git_tree_entry_byname(tree, "") == NULL);
	must_be_true(git_tree_entry_byindex(tree, 0) != NULL);
	must_be_true(git_tree_entry_byindex(tree, 2) != NULL);
	must_be_true(git_tree_entry_byindex(tree, 3) == NULL);
	must_be_true(git_tree_entry_byindex(tree, -1) == NULL);

	git_repository_free(repo);
END_TEST

BEGIN_TEST(read1, "read a tree from the repository")
	git_oid id;
	git_repository *repo;
	git_tree *tree;
	const git_tree_entry *entry;
	git_object *obj;

	must_pass(git_repository_open(&repo, REPOSITORY_FOLDER));

	git_oid_mkstr(&id, tree_oid);

	must_pass(git_tree_lookup(&tree, repo, &id));

	must_be_true(git_tree_entrycount(tree) == 3);

	/* GH-86: git_object_lookup() should also check the type if the object comes from the cache */
	must_be_true(git_object_lookup(&obj, repo, &id, GIT_OBJ_TREE) == 0);
	must_be_true(git_object_lookup(&obj, repo, &id, GIT_OBJ_BLOB) == GIT_EINVALIDTYPE);

	entry = git_tree_entry_byname(tree, "README");
	must_be_true(entry != NULL);

	must_be_true(strcmp(git_tree_entry_name(entry), "README") == 0);

	must_pass(git_tree_entry_2object(&obj, repo, entry));

	git_repository_free(repo);
END_TEST

#if 0
BEGIN_TEST(write0, "write a tree from an index")
	git_repository *repo;
	git_index *index;
	git_oid tree_oid;

	must_pass(git_repository_open(&repo, "/tmp/redtmp/.git"));
	must_pass(git_repository_index(&index, repo));

	must_pass(git_tree_create_fromindex(&tree_oid, index));
	must_pass(print_tree(repo, &tree_oid, 0));

	git_repository_free(repo);
END_TEST
#endif

BEGIN_SUITE(tree)
	//ADD_TEST(print0);
	ADD_TEST(read0);
	ADD_TEST(read1);
	//ADD_TEST(write0);
	//ADD_TEST(write1);
END_SUITE

