/*
 * Copyright (C) 2009-2012 the libgit2 contributors
 *
 * This file is part of libgit2, distributed under the GNU GPL v2 with
 * a Linking Exception. For full terms see the included COPYING file.
 */

#include "iterator.h"
#include "tree.h"
#include "ignore.h"
#include "buffer.h"
#include "git2/submodule.h"

typedef struct tree_iterator_frame tree_iterator_frame;
struct tree_iterator_frame {
	tree_iterator_frame *next;
	git_tree *tree;
	unsigned int index;
};

typedef struct {
	git_iterator base;
	git_repository *repo;
	tree_iterator_frame *stack;
	git_index_entry entry;
	git_buf path;
} tree_iterator;

static const git_tree_entry *tree_iterator__tree_entry(tree_iterator *ti)
{
	return (ti->stack == NULL) ? NULL :
		git_tree_entry_byindex(ti->stack->tree, ti->stack->index);
}

static int tree_iterator__current(
	git_iterator *self, const git_index_entry **entry)
{
	tree_iterator *ti = (tree_iterator *)self;
	const git_tree_entry *te = tree_iterator__tree_entry(ti);

	*entry = NULL;

	if (te == NULL)
		return 0;

	ti->entry.mode = te->attr;
	git_oid_cpy(&ti->entry.oid, &te->oid);
	if (git_buf_joinpath(&ti->path, ti->path.ptr, te->filename) < 0)
		return -1;

	ti->entry.path = ti->path.ptr;

	*entry = &ti->entry;

	return 0;
}

static int tree_iterator__at_end(git_iterator *self)
{
	return (tree_iterator__tree_entry((tree_iterator *)self) == NULL);
}

static tree_iterator_frame *tree_iterator__alloc_frame(git_tree *tree)
{
	tree_iterator_frame *tf = git__calloc(1, sizeof(tree_iterator_frame));
	if (tf != NULL)
		tf->tree = tree;
	return tf;
}

static int tree_iterator__expand_tree(tree_iterator *ti)
{
	int error;
	git_tree *subtree;
	const git_tree_entry *te = tree_iterator__tree_entry(ti);
	tree_iterator_frame *tf;

	while (te != NULL && entry_is_tree(te)) {
		if ((error = git_tree_lookup(&subtree, ti->repo, &te->oid)) < 0)
			return error;

		if ((tf = tree_iterator__alloc_frame(subtree)) == NULL)
			return -1;

		tf->next  = ti->stack;
		ti->stack = tf;

		if (git_buf_joinpath(&ti->path, ti->path.ptr, te->filename) < 0)
			return -1;

		te = tree_iterator__tree_entry(ti);
	}

	return 0;
}

static void tree_iterator__pop_frame(tree_iterator *ti)
{
	tree_iterator_frame *tf = ti->stack;
	ti->stack = tf->next;
	if (ti->stack != NULL) /* don't free the initial tree */
		git_tree_free(tf->tree);
	git__free(tf);
}

static int tree_iterator__advance(
	git_iterator *self, const git_index_entry **entry)
{
	int error = 0;
	tree_iterator *ti = (tree_iterator *)self;
	const git_tree_entry *te = NULL;

	if (entry != NULL)
		*entry = NULL;

	while (ti->stack != NULL) {
		/* remove old entry filename */
		git_buf_rtruncate_at_char(&ti->path, '/');

		te = git_tree_entry_byindex(ti->stack->tree, ++ti->stack->index);
		if (te != NULL)
			break;

		tree_iterator__pop_frame(ti);
	}

	if (te && entry_is_tree(te))
		error = tree_iterator__expand_tree(ti);

	if (!error && entry != NULL)
		error = tree_iterator__current(self, entry);

	return error;
}

static void tree_iterator__free(git_iterator *self)
{
	tree_iterator *ti = (tree_iterator *)self;
	while (ti->stack != NULL)
		tree_iterator__pop_frame(ti);
	git_buf_free(&ti->path);
}

static int tree_iterator__reset(git_iterator *self)
{
	tree_iterator *ti = (tree_iterator *)self;
	while (ti->stack && ti->stack->next)
		tree_iterator__pop_frame(ti);
	if (ti->stack)
		ti->stack->index = 0;
	return tree_iterator__expand_tree(ti);
}

int git_iterator_for_tree(
	git_repository *repo, git_tree *tree, git_iterator **iter)
{
	int error;
	tree_iterator *ti = git__calloc(1, sizeof(tree_iterator));
	GITERR_CHECK_ALLOC(ti);

	ti->base.type    = GIT_ITERATOR_TREE;
	ti->base.current = tree_iterator__current;
	ti->base.at_end  = tree_iterator__at_end;
	ti->base.advance = tree_iterator__advance;
	ti->base.reset   = tree_iterator__reset;
	ti->base.free    = tree_iterator__free;
	ti->repo         = repo;
	ti->stack        = tree_iterator__alloc_frame(tree);

	if ((error = tree_iterator__expand_tree(ti)) < 0)
		git_iterator_free((git_iterator *)ti);
	else
		*iter = (git_iterator *)ti;
	return error;
}


typedef struct {
	git_iterator base;
	git_index *index;
	unsigned int current;
} index_iterator;

static int index_iterator__current(
	git_iterator *self, const git_index_entry **entry)
{
	index_iterator *ii = (index_iterator *)self;
	*entry = git_index_get(ii->index, ii->current);
	return 0;
}

static int index_iterator__at_end(git_iterator *self)
{
	index_iterator *ii = (index_iterator *)self;
	return (ii->current >= git_index_entrycount(ii->index));
}

static int index_iterator__advance(
	git_iterator *self, const git_index_entry **entry)
{
	index_iterator *ii = (index_iterator *)self;
	if (ii->current < git_index_entrycount(ii->index))
		ii->current++;
	if (entry)
		*entry = git_index_get(ii->index, ii->current);
	return 0;
}

static int index_iterator__reset(git_iterator *self)
{
	index_iterator *ii = (index_iterator *)self;
	ii->current = 0;
	return 0;
}

static void index_iterator__free(git_iterator *self)
{
	index_iterator *ii = (index_iterator *)self;
	git_index_free(ii->index);
	ii->index = NULL;
}

int git_iterator_for_index(git_repository *repo, git_iterator **iter)
{
	int error;
	index_iterator *ii = git__calloc(1, sizeof(index_iterator));
	GITERR_CHECK_ALLOC(ii);

	ii->base.type    = GIT_ITERATOR_INDEX;
	ii->base.current = index_iterator__current;
	ii->base.at_end  = index_iterator__at_end;
	ii->base.advance = index_iterator__advance;
	ii->base.reset   = index_iterator__reset;
	ii->base.free    = index_iterator__free;
	ii->current      = 0;

	if ((error = git_repository_index(&ii->index, repo)) < 0)
		git__free(ii);
	else
		*iter = (git_iterator *)ii;
	return error;
}


typedef struct workdir_iterator_frame workdir_iterator_frame;
struct workdir_iterator_frame {
	workdir_iterator_frame *next;
	git_vector entries;
	unsigned int index;
};

typedef struct {
	git_iterator base;
	git_repository *repo;
	size_t root_len;
	workdir_iterator_frame *stack;
	git_ignores ignores;
	git_index_entry entry;
	git_buf path;
	int is_ignored;
} workdir_iterator;

static workdir_iterator_frame *workdir_iterator__alloc_frame(void)
{
	workdir_iterator_frame *wf = git__calloc(1, sizeof(workdir_iterator_frame));
	if (wf == NULL)
		return NULL;
	if (git_vector_init(&wf->entries, 0, git_path_with_stat_cmp) != 0) {
		git__free(wf);
		return NULL;
	}
	return wf;
}

static void workdir_iterator__free_frame(workdir_iterator_frame *wf)
{
	unsigned int i;
	git_path_with_stat *path;

	git_vector_foreach(&wf->entries, i, path)
		git__free(path);
	git_vector_free(&wf->entries);
	git__free(wf);
}

static int workdir_iterator__update_entry(workdir_iterator *wi);

static int workdir_iterator__expand_dir(workdir_iterator *wi)
{
	int error;
	workdir_iterator_frame *wf = workdir_iterator__alloc_frame();
	GITERR_CHECK_ALLOC(wf);

	error = git_path_dirload_with_stat(wi->path.ptr, wi->root_len, &wf->entries);
	if (error < 0 || wf->entries.length == 0) {
		workdir_iterator__free_frame(wf);
		return GIT_ENOTFOUND;
	}

	git_vector_sort(&wf->entries);
	wf->next  = wi->stack;
	wi->stack = wf;

	/* only push new ignores if this is not top level directory */
	if (wi->stack->next != NULL) {
		ssize_t slash_pos = git_buf_rfind_next(&wi->path, '/');
		(void)git_ignore__push_dir(&wi->ignores, &wi->path.ptr[slash_pos + 1]);
	}

	return workdir_iterator__update_entry(wi);
}

static int workdir_iterator__current(
	git_iterator *self, const git_index_entry **entry)
{
	workdir_iterator *wi = (workdir_iterator *)self;
	*entry = (wi->entry.path == NULL) ? NULL : &wi->entry;
	return 0;
}

static int workdir_iterator__at_end(git_iterator *self)
{
	return (((workdir_iterator *)self)->entry.path == NULL);
}

static int workdir_iterator__advance(
	git_iterator *self, const git_index_entry **entry)
{
	int error;
	workdir_iterator *wi = (workdir_iterator *)self;
	workdir_iterator_frame *wf;
	git_path_with_stat *next;

	if (entry != NULL)
		*entry = NULL;

	if (wi->entry.path == NULL)
		return 0;

	while ((wf = wi->stack) != NULL) {
		next = git_vector_get(&wf->entries, ++wf->index);
		if (next != NULL) {
			if (strcmp(next->path, DOT_GIT "/") == 0)
				continue;
			/* else found a good entry */
			break;
		}

		/* pop workdir directory stack */
		wi->stack = wf->next;
		workdir_iterator__free_frame(wf);
		git_ignore__pop_dir(&wi->ignores);

		if (wi->stack == NULL) {
			memset(&wi->entry, 0, sizeof(wi->entry));
			return 0;
		}
	}

	error = workdir_iterator__update_entry(wi);

	if (!error && entry != NULL)
		error = workdir_iterator__current(self, entry);

	return error;
}

static int workdir_iterator__reset(git_iterator *self)
{
	workdir_iterator *wi = (workdir_iterator *)self;
	while (wi->stack != NULL && wi->stack->next != NULL) {
		workdir_iterator_frame *wf = wi->stack;
		wi->stack = wf->next;
		workdir_iterator__free_frame(wf);
		git_ignore__pop_dir(&wi->ignores);
	}
	if (wi->stack)
		wi->stack->index = 0;
	return 0;
}

static void workdir_iterator__free(git_iterator *self)
{
	workdir_iterator *wi = (workdir_iterator *)self;

	while (wi->stack != NULL) {
		workdir_iterator_frame *wf = wi->stack;
		wi->stack = wf->next;
		workdir_iterator__free_frame(wf);
	}

	git_ignore__free(&wi->ignores);
	git_buf_free(&wi->path);
}

static int workdir_iterator__update_entry(workdir_iterator *wi)
{
	git_path_with_stat *ps = git_vector_get(&wi->stack->entries, wi->stack->index);

	git_buf_truncate(&wi->path, wi->root_len);
	if (git_buf_put(&wi->path, ps->path, ps->path_len) < 0)
		return -1;

	memset(&wi->entry, 0, sizeof(wi->entry));
	wi->entry.path = ps->path;

	/* skip over .git directory */
	if (strcmp(ps->path, DOT_GIT "/") == 0)
		return workdir_iterator__advance((git_iterator *)wi, NULL);

	/* if there is an error processing the entry, treat as ignored */
	wi->is_ignored = 1;

	git_index__init_entry_from_stat(&ps->st, &wi->entry);

	/* need different mode here to keep directories during iteration */
	wi->entry.mode = git_futils_canonical_mode(ps->st.st_mode);

	/* if this is a file type we don't handle, treat as ignored */
	if (wi->entry.mode == 0)
		return 0;

	/* okay, we are far enough along to look up real ignore rule */
	if (git_ignore__lookup(&wi->ignores, wi->entry.path, &wi->is_ignored) < 0)
		return 0; /* if error, ignore it and ignore file */

	/* detect submodules */
	if (S_ISDIR(wi->entry.mode)) {
		bool is_submodule = git_path_contains(&wi->path, DOT_GIT);

		/* if there is no .git, still check submodules data */
		if (!is_submodule) {
			int res = git_submodule_lookup(NULL, wi->repo, wi->entry.path);
			is_submodule = (res == 0);
			if (res == GIT_ENOTFOUND)
				giterr_clear();
		}

		/* if submodule, mark as GITLINK and remove trailing slash */
		if (is_submodule) {
			size_t len = strlen(wi->entry.path);
			assert(wi->entry.path[len - 1] == '/');
			wi->entry.path[len - 1] = '\0';
			wi->entry.mode = S_IFGITLINK;
		}
	}

	return 0;
}

int git_iterator_for_workdir(git_repository *repo, git_iterator **iter)
{
	int error;
	workdir_iterator *wi = git__calloc(1, sizeof(workdir_iterator));
	GITERR_CHECK_ALLOC(wi);

	wi->base.type    = GIT_ITERATOR_WORKDIR;
	wi->base.current = workdir_iterator__current;
	wi->base.at_end  = workdir_iterator__at_end;
	wi->base.advance = workdir_iterator__advance;
	wi->base.reset   = workdir_iterator__reset;
	wi->base.free    = workdir_iterator__free;
	wi->repo         = repo;

	if (git_buf_sets(&wi->path, git_repository_workdir(repo)) < 0 ||
		git_path_to_dir(&wi->path) < 0 ||
		git_ignore__for_path(repo, "", &wi->ignores) < 0)
	{
		git__free(wi);
		return -1;
	}

	wi->root_len = wi->path.size;

	if ((error = workdir_iterator__expand_dir(wi)) < 0)
		git_iterator_free((git_iterator *)wi);
	else
		*iter = (git_iterator *)wi;

	return error;
}


int git_iterator_current_tree_entry(
	git_iterator *iter, const git_tree_entry **tree_entry)
{
	*tree_entry = (iter->type != GIT_ITERATOR_TREE) ? NULL :
		tree_iterator__tree_entry((tree_iterator *)iter);
	return 0;
}

int git_iterator_current_is_ignored(git_iterator *iter)
{
	return (iter->type != GIT_ITERATOR_WORKDIR) ? 0 :
		((workdir_iterator *)iter)->is_ignored;
}

int git_iterator_advance_into_directory(
	git_iterator *iter, const git_index_entry **entry)
{
	workdir_iterator *wi = (workdir_iterator *)iter;

	if (iter->type == GIT_ITERATOR_WORKDIR &&
		wi->entry.path &&
		S_ISDIR(wi->entry.mode) &&
		!S_ISGITLINK(wi->entry.mode))
	{
		if (workdir_iterator__expand_dir(wi) < 0)
			/* if error loading or if empty, skip the directory. */
			return workdir_iterator__advance(iter, entry);
	}

	return entry ? git_iterator_current(iter, entry) : 0;
}
