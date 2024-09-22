#include <unistd.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Function to execute the paster2 command and measure its execution time
double measure_execution_time(int B, int P) {
    char command[256];
    struct timeval start, end;

    // Prepare the command
    snprintf(command, sizeof(command), "./findpng3 -t %d -m %d", B, P);

    // Get the start time
    gettimeofday(&start, NULL);

    // Execute the command
    system(command);

    // Get the end time
    gettimeofday(&end, NULL);

    // Calculate the elapsed time in seconds
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1.0e6;

    return elapsed;
}

int main() {
    char hostname[128];
    char filename[256];
    int n_times = 5;
    int num_inputs;
    double total_time;
    double average_time;

    int table[][2] = {
        {1, 1},
        {1, 10},
        {1, 20},
        {1, 30},
        {1, 40},
        {1, 50},
        {1, 100},
        {10, 1},
        {10, 10},
        {10, 20},
        {10, 30},
        {10, 40},
        {10, 50},
        {10, 100},
        {20, 1},
        {20, 10},
        {20, 20},
        {20, 30},
        {20, 40},
        {20, 50},
        {20, 100}
    };

    if (gethostname(hostname, sizeof(hostname)) == -1) {
        perror("gethostname");
        exit(EXIT_FAILURE);
    }
    gethostname(hostname, sizeof(hostname));

    snprintf(filename, sizeof(filename), "lab5_%s.csv", hostname);

    // Open the CSV file for writing
    FILE *fp = fopen(filename, "w");
    if (fp == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    // Write the header to the CSV file
    fprintf(fp, "T,M,Time\n");

    num_inputs = sizeof(table) / sizeof(table[0]);

    // Loop through the table and measure execution times
    for (int i = 0; i < num_inputs; i++) {
        int B = table[i][0];
        int P = table[i][1];

        total_time = 0.0;

        for (int j = 0; j < n_times; j++) {
            total_time += measure_execution_time(B, P);
        }

        average_time = total_time / n_times;

        // Write the results to the CSV file
        fprintf(fp, "%d,%d,%.6f\n", B, P, average_time);
    }

    // Close the CSV file
    fclose(fp);

    return 0;
}