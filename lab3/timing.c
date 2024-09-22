#include <unistd.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Function to execute the paster2 command and measure its execution time
double measure_execution_time(int B, int P, int C, int X, int N) {
    char command[256];
    struct timeval start, end;

    // Prepare the command
    snprintf(command, sizeof(command), "./paster2 %d %d %d %d %d", B, P, C, X, N);

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

    int table[][5] = {
        {5, 1, 1, 0, 1},
        {5, 1, 5, 0, 1},
        {5, 5, 1, 0, 1},
        {5, 5, 5, 0, 1},
        {10, 1, 1, 0, 1},
        {10, 1, 5, 0, 1},
        {10, 1, 10, 0, 1},
        {10, 5, 1, 0, 1},
        {10, 5, 5, 0, 1},
        {10, 5, 10, 0, 1},
        {10, 10, 1, 0, 1},
        {10, 10, 5, 0, 1},
        {10, 10, 10, 0, 1},
        {5, 1, 1, 200, 1},
        {5, 1, 5, 200, 1},
        {5, 1, 10, 200, 1},
        {5, 5, 1, 200, 1},
        {5, 5, 5, 200, 1},
        {10, 1, 1, 200, 1},
        {10, 1, 5, 200, 1},
        {10, 1, 10, 200, 1},
        {10, 5, 1, 200, 1},
        {10, 5, 5, 200, 1},
        {10, 5, 10, 200, 1},
        {10, 10, 1, 200, 1},
        {10, 10, 5, 200, 1},
        {10, 10, 10, 200, 1},
        {5, 1, 1, 400, 1},
        {5, 1, 5, 400, 1},
        {5, 5, 1, 400, 1},
        {5, 5, 5, 400, 1},
        {10, 1, 1, 400, 1},
        {10, 1, 5, 400, 1},
        {10, 1, 10, 400, 1},
        {10, 5, 1, 400, 1},
        {10, 5, 5, 400, 1},
        {10, 5, 10, 400, 1},
        {10, 10, 1, 400, 1},
        {10, 10, 5, 400, 1},
        {10, 10, 10, 400, 1}
    };

    if (gethostname(hostname, sizeof(hostname)) == -1) {
        perror("gethostname");
        exit(EXIT_FAILURE);
    }
    gethostname(hostname, sizeof(hostname));

    snprintf(filename, sizeof(filename), "lab3_%s.csv", hostname);

    // Open the CSV file for writing
    FILE *fp = fopen(filename, "w");
    if (fp == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    // Write the header to the CSV file
    fprintf(fp, "B,P,C,X,N,Time\n");

    num_inputs = sizeof(table) / sizeof(table[0]);

    // Loop through the table and measure execution times
    for (int i = 0; i < num_inputs; i++) {
        int B = table[i][0];
        int P = table[i][1];
        int C = table[i][2];
        int X = table[i][3];
        int N = table[i][4];

        total_time = 0.0;

        for (int j = 0; j < n_times; j++) {
            total_time += measure_execution_time(B, P, C, X, N);
        }

        average_time = total_time / n_times;

        // Write the results to the CSV file
        fprintf(fp, "%d,%d,%d,%d,%d,%.6f\n", B, P, C, X, N, average_time);
    }

    // Close the CSV file
    fclose(fp);

    return 0;
}