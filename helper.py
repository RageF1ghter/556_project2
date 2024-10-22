# Specify the file name
filename = "file.txt"

# Open the file in write mode
with open(filename, 'w') as file:
    # Specify the range of numbers to write
    for number in range(1, 1000000):  # Example: numbers 1 to 100
        file.write("{}\n".format(number))  # Write each number followed by a newline


