/*_____________________________________________________________
 | partial C++ wrapper for libostree
 | - C++ style lifetimes
 | - C++ style usage
 |
 | all methods of classes are converted in the following way:
 | - if viable, output is returned, not passed as pointer
 | - *self is not needed, rather integrated in class method
 | - cancellable is automatically generated & doesn't need to
 |   be passed
 | - errors are thrown, not passed as pointer
 |___________________________________________________________*/

#pragma once
// C++
#include <sys/types.h>
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
// C
#include <fcntl.h>
#include <cerrno>
#include <cstdio>
// external
#include <glib.h>
#include <ostree.h>

namespace cpplibostree {

using Clock = std::chrono::utc_clock;
using Timepoint = std::chrono::time_point<Clock>;

struct Signature {
    bool valid{false};
    bool sigExpired{true};
    bool keyExpired{true};
    bool keyRevoked{false};
    bool keyMissing{true};
    std::string fingerprint;
    std::string fingerprintPrimary;
    Timepoint timestamp;
    Timepoint expireTimestamp;
    std::string pubkeyAlgorithm;
    std::string username;
    std::string usermail;
    Timepoint keyExpireTimestamp;
    Timepoint keyExpireTimestampPrimary;
} __attribute__((aligned(128)));

struct Commit {
    std::string hash;
    std::string contentChecksum;
    std::string subject{"OSTree TUI Error - invalid commit state"};
    std::string body;
    std::string version;
    Timepoint timestamp;
    std::string parent;
    std::string branch;
    std::vector<Signature> signatures;
} __attribute__((aligned(128)));

// map commit hash to commit
using CommitList = std::unordered_map<std::string, Commit>;

/**
 * @brief OSTreeRepo functions as a C++ wrapper around libostree's OstreeRepo.
 * The complete OSTree repository gets parsed into a complete commit list in
 * commitList and a list of refs in branches.
 */
class OSTreeRepo {
   private:
    std::string repoPath;
    CommitList commitList;
    std::vector<std::string> branches;

   public:
    /**
     * @brief Construct a new OSTreeRepo.
     *
     * @param repoPath Path to the OSTree Repository
     */
    explicit OSTreeRepo(std::string repoPath);

    /**
     * @brief Return a C-style pointer to a libostree OstreeRepo. This exists, to be
     * able to access functions, that have not yet been adapted in this C++ wrapper.
     *
     * @return OstreeRepo*
     */
    OstreeRepo* _c();

    /// Getter
    [[nodiscard]] const std::string& GetRepoPath() const;
    /// Getter
    [[nodiscard]] const CommitList& GetCommitList() const;
    /// Getter
    [[nodiscard]] const std::vector<std::string>& GetBranches() const;

    // Methods

    /**
     * @brief Reload the OSTree repository data.
     *
     * @return true if data was changed during the reload
     * @return false if nothing changed
     */
    bool UpdateData();

    /**
     * @brief Check if a certain commit is signed. This simply accesses the
     * size() of commit.signatures.
     *
     * @param commit
     * @return true if the commit is signed
     * @return false if the commit is not signed
     */
    [[nodiscard]] static bool IsCommitSigned(const Commit& commit);

    // read & write access to OSTree repo:

    /**
     * @brief Promotes a commit to another branch. Similar to:
     * `ostree commit --repo=repo -b newRef -s newSubject --tree=ref=hash`
     *
     * @param hash hash of the commit to promote
     * @param newRef branch to promote to
     * @param addMetadataStrings list of metadata strings to add -> KEY=VALUE
     * @param newSubject new commit subject, it needed
     * @param keepMetadata should new commit keep metadata of old commit
     * @return true on success, false on failed promotion
     */
    bool PromoteCommit(const std::string& hash,
                       const std::string& newRef,
                       const std::vector<std::string> addMetadataStrings,
                       const std::string& newSubject = "",
                       bool keepMetadata = true);

    /**
     * @brief Removes a commit (and all its predecessors, if they would)
     *
     * Synonym to:
     *  [ `ostree reset --repo=<repo> <ref> <ref>^` ]
     *  `ostree prune --repo=<repo> --delete-commit=<hash>`
     *
     * Effect:
     *  If the commit is the most recent on its branch -> reset head of branch.
     *  If not, then remove this commit and all predecessors (that would otherwise be unreachable).
     *
     * @param commit Commit to remove (must match an element in the `GetCommitList()`).
     * @return True on success.
     */
    bool RemoveCommitFromBranchAndPrune(const Commit& commit);

    /**
     * @brief Resets the specified branch head by one commit, similar to `git reset HEAD~`
     *
     * @param branch Branch to reset.
     * @return True on success.
     */
    bool ResetBranchHeadAndPrune(const std::string& branch);

    /**
     * @brief Checks if commit is the most recent commit on its branch
     *
     * @param branch Branch to get most recent commit from.
     * @return Most recent commit of the specified branch.
     */
    [[nodiscard]] const Commit& GetMostRecentCommitOfBranch(const std::string& branch) const;

    /**
     * @brief Checks if commit is the most recent commit on its branch
     *
     * @param commit Commit to check.
     * @return True, if commit is most recent on its branch.
     */
    [[nodiscard]] bool IsMostRecentCommitOnBranch(const Commit& commit) const;

    /**
     * @brief Checks if commit is the most recent commit on its branch
     *
     * @param hash Hash of the commit to check.
     * @return True, if commit is most recent on its branch.
     */
    [[nodiscard]] bool IsMostRecentCommitOnBranch(const std::string& hash) const;

   private:
    /**
     * @brief Parse commits from a ostree log output to a commitList, mapping
     * the hashes to commits.
     *
     * @param branch
     * @return std::unordered_map<std::string,Commit>
     */
    CommitList parseCommitsOfBranch(const std::string& branch);

    /**
     * @brief Performs parseCommitsOfBranch() on all available branches and
     * merges all commit lists into one.
     *
     * @return std::unordered_map<std::string,Commit>
     */
    CommitList parseCommitsAllBranches();

    /**
     * @brief Execute a command on the CLI.
     *
     * @warning If possible, use proper `libostree` access, not per command line access.
     *
     * @param command Command to execute.
     * @return true on success
     */
    [[deprecated]] bool runCLICommand(const std::string& command);

    /**
     * @brief Get all branches as a single string, separated by spaces.
     *
     * @return std::string All branch names, separated by spaces
     */
    [[nodiscard]] std::string getBranchesAsString();

    /**
     * @brief Parse a libostree GVariant commit to a C++ commit struct.
     *
     * @param variant pointer to GVariant commit
     * @param branch branch of the commit
     * @param hash commit hash
     * @return Commit struct
     */
    Commit parseCommit(GVariant* variant, const std::string& branch, const std::string& hash);

    /**
     * @brief Parse all commits in a OstreeRepo into a commit vector.
     *
     * @param repo pointer to libostree Ostree repository
     * @param checksum checksum of first commit
     * @param error gets set, if an error occurred during parsing
     * @param commitList commit list to parse the commits into
     * @param branch branch to read the commit from
     * @param isRecurse !Do not use!, or set to false. Used only for recursion.
     * @return true if parsing was successful
     * @return false if an error occurred during parsing
     */
    gboolean parseCommitsRecursive(OstreeRepo* repo,
                                   const gchar* checksum,
                                   GError** error,
                                   CommitList* commitList,
                                   const std::string& branch,
                                   gboolean isRecurse = false);
};

}  // namespace cpplibostree
