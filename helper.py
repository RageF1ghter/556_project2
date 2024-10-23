import os

# Specify the file name
filename = "file.bin"

# Specify the desired file size in bytes (e.g., 10 MB)
file_size = 10 * 1024 * 1024  # 10 MB

# Open the file in binary write mode
with open(filename, 'wb') as file:
    # Define the chunk of binary data to write (in this case, 1 KB of random bytes)
    chunk_size = 1024  # 1 KB
    chunk = os.urandom(chunk_size)  # Generate random binary data of chunk_size

    # Calculate how many chunks are needed to reach the desired file size
    num_chunks = file_size // chunk_size
    remaining_bytes = file_size % chunk_size

    # Write the full chunks
    for _ in range(num_chunks):
        file.write(chunk)

    # Write the remaining bytes (if any)
    if remaining_bytes > 0:
        file.write(os.urandom(remaining_bytes))

print(f"{file_size} bytes binary file '{filename}' created successfully.")
