# Benchmark_DP.py Benchmarking Script

This repository contains a Python script for automating the benchmarking of the [RCKangaroo](https://github.com/RetiredC/RCKangaroo) tool by RetiredCoder, which is used for solving ECDSA private key puzzles using a distinguished point (DP) method. The script is designed to help determine the optimal DP values for different puzzle ranges to maximize the efficiency of solving them.

**Features:**

*   **Automated Benchmarking:** Runs RCKangaroo with a range of DP values for a set of puzzles defined in a CSV file.
*   **Dynamic DP Range Adjustment:** Intelligently adjusts the DP testing range based on the optimal DP found for the previous puzzle. This helps to focus the search on more relevant DP values.
*   **Multiple Loops:** Runs the entire benchmarking process multiple times to collect more statistically significant results.
*   **CSV Output:** Records detailed benchmark results in a CSV file, including puzzle number, start and end keys, DP values, runtimes, success status, optimal DP, and optimal runtime.
*   **Output Redirection:** Redirects the standard output of RCKangaroo to separate text files (one for each execution), keeping the main CSV file cleaner.
*   **Plot Generation:** Creates plots of runtime vs. DP for each puzzle and each loop, making it easy to visualize the performance trends.
*   **Automated Range Calculation:** Calculates the `-range` parameter for RCKangaroo automatically based on the `-start` and `-end` hexadecimal values provided in the puzzle data, eliminating the need for manual bit calculations.

**Prerequisites:**

*   **Python 3:** You need to have Python 3 installed on your system.
*   **RCKangaroo:** You need a modified version of RCKangaroo that supports DP values less than 14 and ranges lower than 32. The original version has these limitations. You can find the modified source code for RCKangaroo here [invalid URL removed]. Compile the RCKangaroo source code according to its instructions.
*   **Required Python Libraries:**
    *   `matplotlib`: For plotting. Install it using pip:

        ```bash
        pip install matplotlib
        ```

**Repository Structure:**

*   **`Benchmark_DP.py`:** The main Python script for running the benchmarks.
*   **`BenchmarkPuzzleList.csv`:** A CSV file containing the puzzle data. You need to create this file (see the format below).
*   **`benchmark_results.csv`:** The output CSV file where the benchmark results will be written.
*   **`rckangaroo_output/`:** A directory where the individual RCKangaroo output files will be stored.
*   **`README.md`:** This file.

**`BenchmarkPuzzleList.csv` Format:**

The `BenchmarkPuzzleList.csv` file should contain the following columns:

*   **`puzzle_number`:** The puzzle number (e.g., 30, 31, 32, ...).
*   **`start_key`:** The starting hexadecimal value of the private key range for the puzzle.
*   **`end_key`:** The ending hexadecimal value of the private key range for the puzzle.
*   **`public_key`:** The target public key for the puzzle.

**Example `BenchmarkPuzzleList.csv`:**

```csv
puzzle_number,start_key,end_key,public_key
30,10000000,0x1fffffff,02591d682c3da4a2a698633bf5751738b67c343285ebdc3492645cb44658911484
31,40000000,0x7fffffff,0387dc70db1806cd9a9a76637412ec11dd998be666584849b3185f7f9313c8fd28
32,80000000,0xffffffff,0209c58240e50e3ba3f833c82655e8725c037a2294e14cf5d73a5df8d56159de69
```

**How to Use:**

1.  **Clone the Repository (Optional):** If you've put the script on GitHub, you can clone your repository to your local machine.
2.  **Compile RCKangaroo:** Download the modified RCKangaroo source code from the repository https://github.com/RetiredC/RCKangaroo, and compile it according to the instructions provided in its README. Place the compiled `RCKangaroo.exe` (or `RCKangaroo` for Linux) in the same directory as the `Benchmark_DP.py`.
3.  **Create `BenchmarkPuzzleList.csv`:** Create your `BenchmarkPuzzleList.csv` file with the puzzle data you want to benchmark.
4.  **Modify Parameters (Optional):**
    *   **`initial_dp_range`:** In the `if __name__ == "__main__":` block of `Benchmark_DP.py`, you can adjust the `initial_dp_range` variable to set the initial range of DP values to test for the first puzzle.
    *   **`num_loops`:** Change the `num_loops` variable to control how many times you want to loop through the entire set of puzzles.
    *   **`gpu`**: Change the `gpu` variable in `Benchmark_DP.py` to the desired GPU to use.
    *   **`timeout`**: Change the `timeout` variable in `Benchmark_DP.py` to the desired timeout for RCKangaroo, especially if it is crashing with the default.
5.  **Run the Script:** Execute the script from your terminal:

    ```bash
    python Benchmark_DP.py
    ```

**Output:**

*   **`benchmark_results.csv`:** This file will contain the detailed benchmark results.
*   **`rckangaroo_output/`:** This directory will contain individual text files with the output of each RCKangaroo execution.
*   **PNG Plots:** The script will generate PNG plots (e.g., `_puzzle30_loop1_runtime.png`) showing runtime vs. DP for each puzzle and loop.

**Notes:**

*   The script assumes that the `RCKangaroo.exe` (or `RCKangaroo`) executable is in the same directory. If it's located elsewhere, you'll need to modify the path in the `command` list within the `execute_rckkangaroo` function of `Benchmark_DP.py`.
*   The optimal DP values found by this script can be used as a starting point for further optimization or for running targeted RCKangaroo instances to solve specific puzzles.
*   The script now includes a timeout parameter within `execute_rckkangaroo` of `Benchmark_DP.py`. The default value is set to 1800 seconds (30 minutes). You can adjust it based on your needs and the expected runtime of each puzzle.

**Disclaimer:**

Benchmarking can be time-consuming. The time it takes to complete will depend on the number of puzzles, the DP range, the number of loops, and the performance of your hardware. The script is provided as-is, and the author makes no guarantees about its accuracy or efficiency. Use it at your own risk.