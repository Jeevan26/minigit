# minigit

`minigit` is a small command-line version control project written in C++. It
supports initializing a repository, staging files, creating commits, viewing
repository status and history, and switching between local branches.

## Requirements

- A C++17-compatible `g++`
- GNU Make
- OpenSSL development libraries (for `libcrypto`)

## Build

From the project root, run:

```sh
make
```

This creates the `mgit` executable in the project root.

## Quick start

Run `mgit` from the directory you want to track:

```sh
./mgit init
./mgit config name "Your Name"
./mgit config email "you@example.com"
printf 'Hello, minigit!\n' > hello.txt
./mgit add hello.txt
./mgit commit "Add greeting"
./mgit status
./mgit log
```

Repository data is stored in a `.mgit` directory inside the current working
directory. Configure both your name and email before creating your first
commit.

## Commands

| Command | Description |
| --- | --- |
| `mgit init` | Initialize a repository with an initial `main` branch. |
| `mgit add <file-or-directory>` | Stage a file or the regular files under a directory. |
| `mgit commit "<message>"` | Create a commit from staged changes. If the message is omitted, minigit prompts for one. |
| `mgit config name "<name>"` | Set the commit author name. |
| `mgit config email "<email>"` | Set the commit author email. |
| `mgit status` | Show the current branch, staged changes, unstaged changes, and untracked files. |
| `mgit log` | Show the current branch's commit history. |
| `mgit checkout <branch>` | Switch to an existing local branch. |
| `mgit checkout -b <branch>` | Create and switch to a new local branch. |
| `mgit destroy` | Delete the `.mgit` repository directory in the current working directory. |

Checkout refuses to switch branches when tracked files have local changes or
staged changes, and it avoids overwriting untracked files. Commit snapshots
include previously committed files as well as the current staged changes.
