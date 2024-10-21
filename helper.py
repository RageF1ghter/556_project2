# Specify the file name
filename = "file.txt"

# Open the file in write mode
with open(filename, 'w') as file:
    # Specify the range of numbers to write
    for number in range(1, 10000):  # Example: numbers 1 to 100
        file.write(f"{number}\n")  # Write each number followed by a newline

print(f"Numbers written to {filename}")
