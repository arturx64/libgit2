#include "clar_libgit2.h"
#include "refs.h"
#include "branch.h"

static git_repository *repo;
static git_strarray branch_list;
static git_reference *fake_remote;

void test_refs_branches_listall__initialize(void)
{
	git_oid id;

	cl_fixture_sandbox("testrepo.git");
	cl_git_pass(git_repository_open(&repo, "testrepo.git"));

	cl_git_pass(git_oid_fromstr(&id, "be3563ae3f795b2b4353bcce3a527ad0a4f7f644"));
	cl_git_pass(git_reference_create_oid(&fake_remote, repo, "refs/remotes/nulltoken/master", &id, 0));
}

void test_refs_branches_listall__cleanup(void)
{
	git_strarray_free(&branch_list);
	git_reference_free(fake_remote);
	git_repository_free(repo);

	cl_fixture_cleanup("testrepo.git");
}

static void assert_retrieval(unsigned int flags, unsigned int expected_count)
{
	cl_git_pass(git_branch_list(&branch_list, repo, flags));

	cl_assert(branch_list.count == expected_count);
}

void test_refs_branches_listall__retrieve_all_branches(void)
{
	assert_retrieval(GIT_BRANCH_LOCAL | GIT_BRANCH_REMOTE, 6 + 1);
}

void test_refs_branches_listall__retrieve_remote_branches(void)
{
	assert_retrieval(GIT_BRANCH_REMOTE, 1);
}

void test_refs_branches_listall__retrieve_local_branches(void)
{
	assert_retrieval(GIT_BRANCH_LOCAL, 6);
}
