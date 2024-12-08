import os
import sys
import matplotlib.pyplot as plt
import numpy as np
import polars
import json
import matplotlib.patches as mpatches
from matplotlib.ticker import FuncFormatter

PURPLE = "\033[35m"
RED = "\033[31m"
RESET = "\033[0m"

data_dir = sys.argv[1]
where_to_save = sys.argv[2]

# Function to format the y-axis ticks
def time_formatter(x, pos):
    if x == 0:
        return "0"
    log10 = np.log10(x)
    if log10 < 0:
        return f'{x:.0f}'
    elif log10 < 3:
        return f'{x:.0f} ns'
    elif log10 < 6:
        return f'{x / 1000:.0f} µs'
    elif log10 < 9:
        return f'{x / 1000000:.0f} ms'
    elif log10 < 12:
        return f'{x / 1000000000:.0f} s'
    
def bytes_formatter(x, pos):
    if x == 0:
        return "0 o"
    log10 = np.log10(x)
    if log10 < 3:
        return str(x) + " o"
    elif log10 < 6:
        return str(int(x) // 1024) + " ko"
    elif log10 < 9:
        return str(int(x) // (1024**2)) + " Mo"
    elif log10 < 12:
        return str(int(x) // (1024**3)) + " Go"
        
# Function to get the min, max, and average of the data
def min_max_average(data):
    times = np.array(data)
    min_times = np.min(times)
    max_times = np.max(times)
    mean_times = np.mean(times)

    return min_times, max_times, mean_times

# Plot parameters for the unit benchmarks
plt.rcParams['font.size'] = 12
plt.rcParams['font.weight'] = 'normal'
plt.rcParams['axes.titlesize'] = 14
plt.rcParams['axes.titleweight'] = 'normal'
plt.rcParams['axes.labelsize'] = 12

units = [bench for bench in os.listdir(data_dir + "/unit")]
for ub in units:
    data = polars.read_csv(data_dir + "/unit/" + ub)

    sizes = data["size"].to_numpy()
    libc_data = data["libc"].to_numpy()
    challoc_data = data["challoc"].to_numpy()

    # set log2 scale for the plot in x-axis and log10 scale for the plot in y-axis
    plt.xscale('log', base=2)
    plt.yscale('log', base=10)

    # set the axis ticks
    x_ticks = [sizes[i] for i in range(0, len(sizes), 6)]
    plt.xticks(x_ticks)
    plt.gca().xaxis.set_major_formatter(FuncFormatter(bytes_formatter))
    plt.gca().yaxis.set_major_formatter(FuncFormatter(time_formatter))

    # Initialize lists to store the mean times for plotting lines
    mean_libc_times_list = []
    mean_challoc_times_list = []

    for size in sizes:
        mean_libc_times = np.mean(libc_data[data["size"] == size])
        mean_challoc_times = np.mean(challoc_data[data["size"] == size])
        mean_libc_times_list.append(mean_libc_times)
        mean_challoc_times_list.append(mean_challoc_times)

        # Plot the data points
        plt.plot(size, mean_libc_times, 'bo')
        plt.plot(size, mean_challoc_times, 'ro')

    # Plot lines between the data points
    plt.plot(sizes, mean_libc_times_list, 'b-', label='libc')
    plt.plot(sizes, mean_challoc_times_list, 'r-', label='challoc')

    # Remove extension from the directory name
    ub = ub.replace(".csv", "")

    plt.title(ub)
    plt.legend()

    plt.savefig(where_to_save + '/' + ub + '.svg')

    # Clear the plot for the next iteration
    plt.clf()


# Plot parameters for the program benchmarks
plt.rcParams['font.size'] = 24
plt.rcParams['axes.titlesize'] = 26
plt.rcParams['axes.labelsize'] = 25

programs = [bench for bench in os.listdir(data_dir + "/program")]
for prog in programs:
    data = json.load(open(data_dir + "/program/" + prog))

    libc_times = data["libc"]["times"]
    challoc_times = data["challoc"]["times"]

    (min_libc_times, max_libc_times, mean_libc_times) = min_max_average(libc_times)
    (min_challoc_times, max_challoc_times, mean_challoc_times) = min_max_average(challoc_times)

    libc_memory = data["libc"]["memory"]
    challoc_memory = data["challoc"]["memory"]

    # Plot the min as light, average as regular, and max as dark (blue or red), all superposed as a bar chart
    # Separate the libc and challoc data with a small gap
    plt.bar(0, max_libc_times, color="darkblue")
    plt.bar(0, mean_libc_times, color="blue")
    plt.bar(0, min_libc_times, color="lightblue")

    plt.bar(1, max_challoc_times, color="darkred")
    plt.bar(1, mean_challoc_times, color="red")
    plt.bar(1, min_challoc_times, color="lightcoral")

    # Replace the 0 xtick with "libc" and 1 xtick with "challoc"
    plt.xticks([0, 1], ["libc", "challoc"])

    # Replace the y-axis ticks with the time_formatter function
    plt.gca().yaxis.set_major_formatter(FuncFormatter(time_formatter))
    # Change prog title from snake_case to Normal Case
    prog = prog.replace(".json", "")

    prog_t = prog.replace("_", " ").title()
    prog_t = prog_t.replace("0X1", "0x1") # lowercase for those
    plt.title(prog_t)

    # Add a legend which explains the colors (lightgrey, grey, darkgrey)
    min_p = mpatches.Patch(color='lightgrey', label='Min')
    mean_p = mpatches.Patch(color='grey', label='Moy')
    max_p = mpatches.Patch(color='black', label='Max')
    # Place legend in the middle
    plt.legend(handles=[max_p, mean_p, min_p], loc='upper center',
          ncol=1, fancybox=True, shadow=True)

    plt.tight_layout()

    plt.savefig(where_to_save + '/' + prog + '_time.svg', bbox_inches='tight')
    plt.clf()

    # Plot memory usage
    plt.bar(0, libc_memory, color="blue")
    plt.bar(1, challoc_memory, color="red")

    plt.xticks([0, 1], ["libc", "challoc"])
    plt.gca().yaxis.set_major_formatter(FuncFormatter(bytes_formatter))
    # Change prog title from snake_case to Normal Case
    prog_t = prog.replace("_", " ").title()
    plt.title(prog_t)

    plt.tight_layout()
    plt.savefig(where_to_save + '/' + prog + '_memory.svg', bbox_inches='tight')
    plt.clf()

# Backup all the benchmark data too
os.system("tar -czf results_backup.tgz " + data_dir)
os.system("mv results_backup.tgz " + where_to_save)