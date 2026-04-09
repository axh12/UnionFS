#!/bin/bash
# Mini-UnionFS CoW Test Script

echo "========================================"
echo "Mini-UnionFS Copy-on-Write Test"
echo "========================================"

# Setup test directories
echo "Setting up test directories..."
mkdir -p lower upper mnt

# Create test file in lower directory
echo "Creating test file in lower directory..."
echo "Original content from lower dir" > lower/test.txt

# Mount the filesystem
echo "Mounting Mini-UnionFS..."
./mini_unionfs lower upper mnt &
sleep 2

# Test 1: Read from mount point
echo ""
echo "Test 1: Reading from mount point"
echo "Content at /mnt/test.txt:"
cat mnt/test.txt

# Test 2: Modify through mount point (triggers CoW)
echo ""
echo "Test 2: Modifying through mount point (CoW triggered)"
echo "Appending to mnt/test.txt..."
echo "Modified content added" >> mnt/test.txt

echo "Content at /mnt/test.txt after modification:"
cat mnt/test.txt

# Test 3: Check upper directory (should have the copy)
echo ""
echo "Test 3: Checking upper directory"
echo "Content in upper/test.txt (should be modified):"
cat upper/test.txt

# Test 4: Check lower directory (should be unchanged)
echo ""
echo "Test 4: Checking lower directory"
echo "Content in lower/test.txt (should be original):"
cat lower/test.txt

# Cleanup
echo ""
echo "Cleaning up..."
fusermount -u mnt
rm -rf lower upper mnt

echo ""
echo "========================================"
echo "CoW Test Complete!"
echo "========================================"