#include "paths.hpp"

namespace mgit::paths
{
    const std::filesystem::path repository_dir = ".mgit";
    const std::filesystem::path objects_dir = repository_dir / "objects";
    const std::filesystem::path refs_heads_dir = repository_dir / "refs/heads";
    const std::filesystem::path refs_heads_reference = refs_heads_dir.lexically_relative(repository_dir);
    const std::filesystem::path config_file = repository_dir / "config";
    const std::filesystem::path head_file = repository_dir / "HEAD";
    const std::filesystem::path index_file = repository_dir / "minigit-index";
}
