"""
Automates benchmarking of RCKangaroo.exe, analyzes results, and redirects output to files.
Writes results to CSV after each puzzle is benchmarked.
Dynamically adjusts dp_range based on the optimal DP of the previous puzzle.
Loops through all puzzles 'num_loops' times.

RCKangaroo.exe -dp 19 -gpu 0 -start 40000000000000000 -range 66 -pubkey 025ff74e8e2d3421f3895dd0c54b7aa9709d466df8245dfe24f836152ab0a76ac2 -tames 75tames.dat -max 10
RCKangaroo.exe -dp 19 -gpu 0 -start 4000000000000000000 -range 74 -pubkey 03384c53455314130dfdae087df8dba06a0a8dcd6c9af970f05b8eaf60ca51c875 -tames 75tames.dat -max 10
RCKangaroo.exe -dp 44 -gpu 0 -start 4000000000000000000000000000000000 -range 134 -pubkey 02145d2611c823a396ef6712ce0f712f09b9b4f3135e3e0aa3230fb9b6d08d1e16 -tames 135_44_tames.dat -max 10
RCKangaroo.exe -dp 24 -range 66 -tames 67_24_tames.dat -max 10
"""

import subprocess
import time
import csv
import os
import matplotlib.pyplot as plt

# User-Defined Variables
max_ops = 10                                  # Maximum number of operations for benchmarking
initial_dp_lower_range = 12                   # Initial lower range for DP testing
initial_dp_upper_range = 18                   # Initial upper range for DP testing
initial_end_hex = "0x3ffffffffffffffff"       # Default initial end key range in hex
target_bit = 66                               # Prefix for folders and files
prefix = "_"
timeout_var = 480                             # Timeout duration in seconds
num_loops = 2                                 # Number of loops to repeat benchmarking
optimal_dp_lower_delta = 3                    # Number of DP steps below previous optimal DP to test
optimal_dp_upper_delta = 3                    # Number of DP steps above previous optimal DP to test
lowest_dp_allowed = 4                         # Lowest DP step allowed to test
tames_file = f"{target_bit}_tames.dat"        # Set Tames Name

# File and Directory Names
file_prefix = f"{target_bit}{prefix}"
output_file = f"{file_prefix}benchmark_results.csv"                  # CSV file for benchmark results
output_dir = f"{file_prefix}rckangaroo_output"                       # Directory for RCKangaroo output files
figures_dir = f"{file_prefix}Figures"                                # Directory for generated plots
puzzle_data_file = f"{file_prefix}bitcoin_puzzle_addresses.csv"      # CSV file with puzzle data


def calculate_bit_range(start_hex, end_hex=initial_end_hex):
    """
    Calculate the bit range for the interval between start_hex and end_hex.
    """
    start = int(start_hex, 16)
    end = int(end_hex, 16)
    if end <= start:
        raise ValueError("End value must be greater than Start value")
    return (end - start).bit_length()


def execute_rckangaroo(dp, start_hex, pubkey, end_hex, output_dir, gpu="0"):
    """
    Executes RCKangaroo.exe with the specified parameters, measures performance,
    and redirects output to a file.
    """
    bit_range = calculate_bit_range(start_hex, end_hex)
    tames_file = f"{target_bit}_{dp}_tames.dat"        # Set Tames Filename with Bit Range and DP Prefix

    command = [
        "./RCKangaroo" if os.name != "nt" else "RCKangaroo.exe",
        "-dp", str(dp),
        "-gpu", gpu,
        "-start", start_hex,
        "-range", str(bit_range),
        "-pubkey", pubkey,
        "-tames", tames_file,
        "-max", str(max_ops)
    ]

    # Debugging: Print the command being executed
    print(f"Executing: {' '.join(command)}")

    # Create a unique output file for each execution
    timestamp = time.strftime("%Y%m%d-%H%M%S")
    output_filename = os.path.join(output_dir, f"puzzle{puzzle_number}_dp{dp}_{timestamp}.txt")

    with open(output_filename, "w") as output_file:
        start_time = time.time()
        try:
            result = subprocess.run(command, stdout=output_file, stderr=subprocess.PIPE, text=True, timeout=timeout_var)
            end_time = time.time()

            runtime = end_time - start_time
            success = result.returncode == 0

            return {
                "DP": dp,
                "Runtime": runtime,
                "Success": success,
                "Output_File": output_filename  # Store the output file name
            }

        except subprocess.TimeoutExpired:
            return {"DP": dp, "Runtime": None, "Success": False, "Output_File": output_filename}


def benchmark_rckangaroo(puzzle_data, initial_dp_range, num_loops, output_file, output_dir, figures_dir, gpu="0"):
    # Create necessary directories
    os.makedirs(output_dir, exist_ok=True)
    os.makedirs(figures_dir, exist_ok=True)

    # Create the output file with header if it doesn't exist
    if not os.path.exists(output_file):
        with open(output_file, "w", newline="") as csvfile:
            fieldnames = ["Loop", "Puzzle", "Start_Key", "End_Key", "DP", "Runtime", "Success", "Output_File", "Optimal_DP", "Optimal_Runtime"]
            writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
            writer.writeheader()

    previous_optimal_dp = None

    for loop in range(1, num_loops + 1):
        print(f"Starting Loop {loop}...")
        for puzzle in puzzle_data:
            global puzzle_number
            puzzle_number = puzzle['puzzle_number']
            start_hex = puzzle['start_key']
            pubkey = puzzle['public_key']
            end_hex = puzzle['end_key']

            puzzle_results = []
            optimal_dp = None
            optimal_runtime = float('inf')

            print(f"Benchmarking Puzzle #{puzzle_number} (Loop {loop})...")

            # Dynamically adjust dp_range
            if previous_optimal_dp is not None:
                dp_range = range(
                    max(lowest_dp_allowed, previous_optimal_dp - optimal_dp_lower_delta),
                    previous_optimal_dp + optimal_dp_upper_delta + 1
                )
            else:
                dp_range = initial_dp_range

            for dp in dp_range:
                print(f"  Testing DP={dp}...")
                result = execute_rckangaroo(dp, start_hex, pubkey, end_hex, output_dir, gpu)
                result["Loop"] = loop
                result["Puzzle"] = puzzle_number
                result["Start_Key"] = start_hex
                result["End_Key"] = end_hex

                puzzle_results.append(result)

                if result["Success"] and result["Runtime"] is not None and result["Runtime"] < optimal_runtime:
                    optimal_runtime = result["Runtime"]
                    optimal_dp = dp

            for res in puzzle_results:
                res["Optimal_DP"] = optimal_dp
                res["Optimal_Runtime"] = optimal_runtime

            with open(output_file, "a", newline="") as csvfile:
                fieldnames = ["Loop", "Puzzle", "Start_Key", "End_Key", "DP", "Runtime", "Success", "Output_File", "Optimal_DP", "Optimal_Runtime"]
                writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
                writer.writerows(puzzle_results)

            print(f"Puzzle #{puzzle_number} (Loop {loop}) benchmarking completed. Results appended to {output_file}.")
            analyze_results(puzzle_results, output_file, figures_dir, loop)
            previous_optimal_dp = optimal_dp


def analyze_results(results, output_file, figures_dir, loop):
    dp_values = [res["DP"] for res in results if res["Success"]]
    runtimes = [res["Runtime"] for res in results if res["Success"]]
    puzzle_number = results[0]["Puzzle"]

    if len(runtimes) > 0:
        optimal_index = runtimes.index(min(runtimes))
        optimal_dp = dp_values[optimal_index]
        optimal_runtime = runtimes[optimal_index]
        print(f"Puzzle #{puzzle_number} (Loop {loop}): Optimal DP: {optimal_dp} with runtime {optimal_runtime:.2f} seconds.")

        timestamp = time.strftime("%Y%m%d-%H%M%S")
        plt.figure()
        plt.plot(dp_values, runtimes, marker="o")
        plt.title(f"Runtime vs DP for Puzzle #{puzzle_number} (Loop {loop})")
        plt.xlabel("DP")
        plt.ylabel("Runtime (seconds)")
        plt.grid()
        plt.savefig(os.path.join(figures_dir, f"puzzle{file_prefix}_loop{loop}_runtime_{timestamp}.png"))
        plt.close()


def read_puzzle_data(csv_file):
    puzzle_data = []
    with open(csv_file, "r") as csvfile:
        reader = csv.DictReader(csvfile)
        for row in reader:
            puzzle_data.append(row)
    return puzzle_data


if __name__ == "__main__":
    puzzle_data = read_puzzle_data(puzzle_data_file)
    initial_dp_range = range(initial_dp_lower_range, initial_dp_upper_range + 1)
    benchmark_rckangaroo(puzzle_data, initial_dp_range, num_loops, output_file, output_dir, figures_dir)
