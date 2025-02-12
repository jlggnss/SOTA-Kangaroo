import os
import subprocess
import threading
import tkinter as tk
from tkinter import messagebox, ttk, filedialog, scrolledtext
import math

# Function to calculate the bit range
def calculate_bit_range(start_hex, end_hex):
    try:
        start_dec = int(start_hex, 16)
        end_dec = int(end_hex, 16)
        if end_dec <= start_dec:
            raise ValueError("END value must be greater than START value")
        return (end_dec - start_dec).bit_length()
    except ValueError as e:
        raise ValueError(f"Invalid hexadecimal input: {e}")

# Function to get the executable name
def get_executable():
    return "RCKangaroo.exe" if os.name == "nt" else "./RCKangaroo"

# Function to build the command based on GUI input
def build_command():
    """Builds the command list based on the GUI input."""
    dp = dp_entry.get()
    gpu = gpu_entry.get()
    pubkey = pubkey_entry.get()
    start_hex = start_entry.get()
    end_hex = end_entry.get()
    max_ram = max_ram_entry.get()
    max_ops_factor = max_ops_entry.get()
    tames_file = tames_file_entry.get()

    command = [get_executable(), "-dp", dp]

    if gpu:
        command.extend(["-gpu", gpu])
    if pubkey:
        if not start_hex or not end_hex:
           return []
        try:
            bit_range = calculate_bit_range(start_hex, end_hex)
        except ValueError:
            return []
        if not (29 <= bit_range <= 160):
            return []
        command.extend(["-range", str(bit_range), "-start", start_hex, "-pubkey", pubkey])

    if max_ops_factor:
        try:
            float(max_ops_factor)
            command.extend(["-max", max_ops_factor])
        except ValueError:
            return []

    if tames_file:
        command.extend(["-tames", tames_file])

    return command

# Function to execute the command in a separate thread
def execute_command_in_thread(command):
    try:
        subprocess.run(command)
        messagebox.showinfo("Execution Complete", "Command executed successfully!")
    except Exception as e:
        messagebox.showerror("Execution Error", f"An error occurred: {e}")

## Disabled due to Piping info not getting passed - Need to revisit this
"""def execute_command_in_thread(command):
    try:
        # Use Popen with PIPE to capture output in real-time
        process = subprocess.Popen(
            command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1, universal_newlines=True
        )

        # Create a new top-level window for output
        output_window = tk.Toplevel(root)
        output_window.title("RCKangaroo Output")
        output_window.geometry("800x600")  # Adjust size as needed

        # Add a ScrolledText widget to the output window
        output_text = scrolledtext.ScrolledText(output_window, wrap=tk.WORD, state="disabled")
        output_text.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        def append_output(text, tag=None):
            output_text.config(state="normal")
            output_text.insert(tk.END, text, tag)
            output_text.config(state="disabled")
            output_text.see(tk.END)  # Auto-scroll to the end

        # Read output line by line in real-time
        for line in process.stdout:
            append_output(line)

        # Read any remaining error output
        for line in process.stderr:
             append_output(line, 'error')


        process.wait() # Wait for process to complete.
        return_code = process.returncode

        if return_code != 0:
             append_output(f"\nRCKangaroo exited with error code: {return_code}", 'error')
        else:
             append_output("\nRCKangaroo completed successfully.")



    except Exception as e:
        messagebox.showerror("Execution Error", f"An error occurred: {e}")
        return  # Make sure to return after an error
"""

def execute_command():
    """Executes the command."""
    command = build_command()
    if not command:
        messagebox.showerror("Validation Error", "Invalid parameters for the command, check inputs.")
        return
    threading.Thread(target=execute_command_in_thread, args=(command,), daemon=True).start()

def calculate_optimal_dp(start_hex, end_hex, max_ram=8):
    try:
        start = int(start_hex, 16)
        end = int(end_hex, 16)
    except ValueError:
        raise ValueError("Invalid hexadecimal input")

    if end <= start:
        raise ValueError("End value must be greater than Start value")
    range_bits = (end - start).bit_length()
    ops = math.sqrt(math.pi * (end - start) / 2)

    best_dp = -1
    best_ram_usage = float('inf')

    for dp in range(4, 61):
        dp_val = 2 ** dp
        ram_usage_gb = (2 * (range_bits + dp + 64) * ops / dp_val + 512 * 1024 * 1024) / (1024**3)

        if ram_usage_gb <= float(max_ram) * 0.9:
            if best_dp == -1 or dp < best_dp:
                best_dp = dp
                best_ram_usage = ram_usage_gb

    if best_dp == -1:
        raise ValueError(f"No suitable DP found within memory constraints ({max_ram}GB).  Try increasing available RAM or reducing the range.")

    return best_dp, ops

def update_command_display(*args):
    """Updates the command_display with the current command string."""
    try:
        command = build_command()
        command_str = " ".join(command)

        command_display.config(state="normal")
        command_display.delete("1.0", tk.END)
        command_display.insert(tk.INSERT, command_str)
        command_display.config(state="disabled")

    except Exception as e:
        command_display.config(state="normal")
        command_display.delete("1.0", tk.END)
        command_display.insert(tk.INSERT, "Error building command")
        command_display.config(state="disabled")

def update_dp(*args):
    """Updates DP and ops, and also the command display."""
    try:
        start_hex = start_var.get()
        end_hex = end_var.get()
        max_ram = max_ram_var.get()

        if not start_hex.startswith("0x"):
            start_hex = "0x" + start_hex
        if not end_hex.startswith("0x"):
            end_hex = "0x" + end_hex

        if not max_ram.isdigit() or int(max_ram) <= 0:
            raise ValueError("Max RAM must be a positive integer.")

        suggested_dp, ops = calculate_optimal_dp(start_hex, end_hex, max_ram)
        dp_var.set(str(suggested_dp))
        ops_var.set(format_operations(ops))
    except Exception as e:
      dp_var.set("Error")
      ops_var.set("Error")
    finally:
      update_command_display()

def format_operations(num):
    if isinstance(num, str):
        return num
    num = int(round(num))
    return f"{num:,}"

def add_editing_shortcuts(widget):
    menu = tk.Menu(root, tearoff=0)
    menu.add_command(label="Cut", command=lambda: widget.event_generate("<<Cut>>"))
    menu.add_command(label="Copy", command=lambda: widget.event_generate("<<Copy>>"))
    menu.add_command(label="Paste", command=lambda: widget.event_generate("<<Paste>>"))
    menu.add_separator()
    if isinstance(widget, (tk.Entry, tk.Text)):
        menu.add_command(label="Undo", command=lambda: widget.event_generate("<<Undo>>"))
        menu.add_command(label="Redo", command=lambda: widget.event_generate("<<Redo>>"))
        menu.add_separator()
    menu.add_command(label="Select All", command=lambda: select_all(widget))

    def show_menu(event):
        menu.post(event.x_root, event.y_root)

    widget.bind("<Button-3>", show_menu)

def select_all(widget):
    if isinstance(widget, tk.Entry):
        widget.select_range(0, tk.END)
        widget.icursor(tk.END)
    elif isinstance(widget, tk.Text):
        widget.tag_add(tk.SEL, "1.0", tk.END)
        widget.mark_set(tk.INSERT, "1.0")
        widget.see(tk.INSERT)

def browse_file():
    current_directory = os.getcwd()
    filepath = filedialog.asksaveasfilename(
        initialdir=current_directory,
        title="Select a Tames File",
        filetypes=(("Tames files", "*.dat*"), ("All files", "*.*")),
        defaultextension=".dat"
    )
    if filepath:
        filename = os.path.basename(filepath)
        tames_file_var.set(filename)
        tames_file_entry.delete(0, tk.END)
        tames_file_entry.insert(0, filename)
        update_command_display()

# --- GUI Setup ---
# Defaults are Puzzle 66
DEFAULTS = {
    "gpu": "0",
    "pubkey": "024ee2be2d4e9f92d2f5a4a03058617dc45befe22938feed5b7a6b7282dd74cbdd",
    "start": "20000000000000000",
    "end": "3ffffffffffffffff",
    "max_ram": "8",
    "max_ops": "",
    "tames_file": "",
    "command" : ""
}

try:
    DEFAULTS["dp"], DEFAULTS["ops"] = calculate_optimal_dp(DEFAULTS["start"], DEFAULTS["end"], DEFAULTS["max_ram"])
    DEFAULTS["ops"] = format_operations(DEFAULTS["ops"])

except Exception:
    DEFAULTS["dp"] = "Error"
    DEFAULTS["ops"] = "Error"

root = tk.Tk()
root.title("RCKangaroo Parameters Input Dialog")

# --- Create StringVars with defaults ---
dp_var = tk.StringVar(value=DEFAULTS["dp"])
gpu_var = tk.StringVar(value=DEFAULTS["gpu"])
pubkey_var = tk.StringVar(value=DEFAULTS["pubkey"])
start_var = tk.StringVar(value=DEFAULTS["start"])
end_var = tk.StringVar(value=DEFAULTS["end"])
max_ram_var = tk.StringVar(value=DEFAULTS["max_ram"])
ops_var = tk.StringVar(value=DEFAULTS["ops"])
max_ops_var = tk.StringVar(value=DEFAULTS["max_ops"])
tames_file_var = tk.StringVar(value=DEFAULTS["tames_file"])
command_var = tk.StringVar(value=DEFAULTS["command"])

# --- Traces ---
start_var.trace_add("write", update_dp)
end_var.trace_add("write", update_dp)
max_ram_var.trace_add("write", update_dp)
dp_var.trace_add("write", update_command_display)
gpu_var.trace_add("write", update_command_display)
pubkey_var.trace_add("write", update_command_display)
start_var.trace_add("write", update_command_display)
end_var.trace_add("write", update_command_display)
max_ram_var.trace_add("write", update_command_display)
ops_var.trace_add("write", update_command_display)
max_ops_var.trace_add("write", update_command_display)
tames_file_var.trace_add("write", update_command_display)

# --- Create and place widgets ---
style = ttk.Style()
style.configure('TLabel', font=('Arial', 10))
style.configure('TEntry', font=('Arial', 10))
style.configure('TButton', font=('Arial', 10))

# --- Row 0 ---
ttk.Label(root, text="-dp (4-60):", style='TLabel').grid(row=0, column=0, sticky="e", padx=5, pady=5)
dp_entry = ttk.Entry(root, width=10, textvariable=dp_var, justify="center", style='TEntry')
dp_entry.grid(row=0, column=1, sticky="ew", padx=5, pady=2)
add_editing_shortcuts(dp_entry)

ttk.Label(root, text="-gpu (Optional):", style='TLabel').grid(row=0, column=2, sticky="e", padx=5, pady=5)
gpu_entry = ttk.Entry(root, width=10, justify="center", textvariable=gpu_var, style='TEntry')
gpu_entry.grid(row=0, column=3, sticky="ew", padx=5, pady=2)
add_editing_shortcuts(gpu_entry)

# --- Row 1 ---
ttk.Label(root, text="-pubkey (Optional):", style='TLabel').grid(row=1, column=0, sticky="e", padx=5, pady=5)
pubkey_entry = ttk.Entry(root, width=80, justify="left", textvariable=pubkey_var, style='TEntry')
pubkey_entry.grid(row=1, column=1, columnspan=4, sticky="ew", padx=5, pady=2)
add_editing_shortcuts(pubkey_entry)

# --- Row 2 ---
ttk.Label(root, text="-start (Hex, Optional):", style='TLabel').grid(row=2, column=0, sticky="e", padx=5, pady=5)
start_entry = ttk.Entry(root, width=64, textvariable=start_var, justify="left", style='TEntry')
start_entry.grid(row=2, column=1, columnspan=4, sticky="ew", padx=5, pady=2)
add_editing_shortcuts(start_entry)

# --- Row 3 ---
ttk.Label(root, text="-end (Hex, Optional):", style='TLabel').grid(row=3, column=0, sticky="e", padx=5, pady=5)
end_entry = ttk.Entry(root, width=64, textvariable=end_var, justify="left", style='TEntry')
end_entry.grid(row=3, column=1, columnspan=4, sticky="ew", padx=5, pady=2)
add_editing_shortcuts(end_entry)

# --- Row 4 ---
ttk.Label(root, text="Max RAM (GB):", style='TLabel').grid(row=4, column=0, sticky="e", padx=5, pady=5)
max_ram_entry = ttk.Entry(root, width=10, textvariable=max_ram_var, justify="center", style='TEntry')
max_ram_entry.grid(row=4, column=1, sticky="ew", padx=5, pady=2)
add_editing_shortcuts(max_ram_entry)

ttk.Label(root, text="Max Operations:", style='TLabel').grid(row=4, column=2, sticky="e", padx=5, pady=5)
ttk.Label(root, textvariable=ops_var, style='TLabel').grid(row=4, column=3, sticky="ew", padx=5, pady=2)

# --- Row 5 ---
ttk.Label(root, text="Max Ops Factor:", style='TLabel').grid(row=5, column=0, sticky="e", padx=5, pady=5)
max_ops_entry = ttk.Entry(root, width=10, textvariable=max_ops_var, justify="center", style='TEntry')
max_ops_entry.grid(row=5, column=1, sticky="ew", padx=5, pady=2)
add_editing_shortcuts(max_ops_entry)

ttk.Label(root, text="Tames File:", style='TLabel').grid(row=5, column=2, sticky="e", padx=5, pady=5)
tames_file_entry = ttk.Entry(root, width=30, textvariable=tames_file_var, justify="left", style='TEntry')
tames_file_entry.grid(row=5, column=3, sticky="ew", padx=5, pady=2)
add_editing_shortcuts(tames_file_entry)

browse_button = ttk.Button(root, text="Browse...", command=browse_file, style='TButton')
browse_button.grid(row=5, column=4, sticky="ew", padx=5, pady=2)

# --- Row 6 --- (Command Display)
#ttk.Label(root, text="Command:", style='TLabel').grid(row=6, column=0, sticky="e", padx=5, pady=5)
command_display = scrolledtext.ScrolledText(root, wrap=tk.WORD, state="disabled", width=60, height=3, font=('Arial', 10))
command_display.grid(row=6, column=0, columnspan=5, sticky="nsew", padx=5, pady=2)
add_editing_shortcuts(command_display)

# --- Row 7 --- (Execute Button)
execute_button = ttk.Button(root, text="Execute RCKangaroo.exe", command=execute_command, style='TButton')
execute_button.grid(row=7, column=0, columnspan=5, pady=10, sticky="ew")


# --- Configure Row and Column Weights ---
for i in range(8):
    root.rowconfigure(i, weight=1)

root.columnconfigure(1, weight=1)
root.columnconfigure(2, weight=1)
root.columnconfigure(3, weight=1)
root.columnconfigure(4, weight=1)


# Initial command display update
update_command_display()

root.mainloop()
