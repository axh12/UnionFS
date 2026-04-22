# UnionFS using FUSE3

A basic **Union File System (UnionFS)** implemented in user space using **FUSE3**. It merges two directories — a **lower (read-only)** layer and an **upper (writable)** layer — into a single unified view, demonstrating core filesystem concepts such as **Copy-on-Write (COW)** and **whiteout-based deletion**.

---

## Features

- Layered filesystem (lower + upper)
- Copy-on-Write for file modifications
- Whiteout mechanism for deletion
- Merged directory listing
- User-space implementation via FUSE3

---

## Architecture

```
User → Mountpoint → FUSE → UnionFS → Lower + Upper
```

| Layer          | Description                         |
|----------------|-------------------------------------|
| **Lower**      | Original files (read-only)          |
| **Upper**      | Modified and new files (writable)   |
| **Mountpoint** | Unified view presented to the user  |

---

## How It Works

| Operation | Behavior                                               |
|-----------|--------------------------------------------------------|
| **Read**  | Reads from upper if it exists, otherwise from lower    |
| **Write** | Copies file to upper layer first, then modifies it     |
| **Delete**| Creates `.wh.<filename>` whiteout file in upper layer  |
| **List**  | Merges both directories, suppressing duplicates        |

---

## Directory Structure

```
.
├── src/
│   ├── main.c
│   ├── unionfs_cow.c
│   └── operations.h
├── Makefile
└── README.md
```

---

## Requirements

- Linux system
- FUSE3

### Install FUSE3

```bash
sudo apt install libfuse3-dev
```

---

## Build

```bash
make clean
make
```

---

## Usage

### 1. Setup

```bash
mkdir lower upper mountpoint
echo "hello" > lower/test.txt
```

### 2. Run

```bash
./mini_unionfs lower upper mountpoint -f -o allow_other
```

---

## Demonstration

### Read

```bash
cat mountpoint/test.txt
```

### Write (Copy-on-Write)

```bash
echo "modified" > mountpoint/test.txt
```

Verify the COW behavior:

```bash
cat lower/test.txt   # unchanged: "hello"
cat upper/test.txt   # modified:  "modified"
```

### Delete (Whiteout)

```bash
rm mountpoint/test.txt
ls mountpoint        # empty — file is hidden
ls -a upper/         # shows: .wh.test.txt
```

---

## Key Concepts

### Copy-on-Write (COW)

When a file from the lower layer is modified, it is first copied to the upper layer. All subsequent changes are applied to that upper copy, leaving the original lower-layer file untouched.

### Whiteout Files

Deletion is handled using hidden marker files named `.wh.<filename>`. These whiteout files signal the merged view to suppress the corresponding lower-layer file, making it appear deleted without actually removing it.

### FUSE

FUSE (Filesystem in Userspace) allows filesystem implementations to run entirely in user space, without any kernel modifications. FUSE3 is the current major version of this interface.

---

## Limitations

- Limited support for nested directories
- Basic error handling only
- No performance optimizations

---

## Contribution

- FUSE3 setup and configuration
- Integration of filesystem modules
- Copy-on-Write implementation
- Whiteout handling and path resolution
