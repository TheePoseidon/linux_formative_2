#!/bin/bash

mkdir -p logs
sizes=(1 2 3 5 8 10 15 20 25 30 40 50)

for i in "${!sizes[@]}"; do
    name=$(printf "logs/file%02d.log" $((i + 1)))
    awk -v mb="${sizes[$i]}" -v seed=$((i + 1)) 'BEGIN {
        srand(seed)
        split("INFO INFO INFO INFO WARN WARN ERROR DEBUG", levels, " ")
        limit = mb * 1024 * 1024
        bytes = 0
        n = 0
        while (bytes < limit) {
            n++
            level = levels[int(rand() * 8) + 1]
            s = sprintf("2026-11-13 12:%02d:%02d [%s] request %d handled by server-%d\n", int(rand() * 60), int(rand() * 60), level, n, int(rand() * 10))
            printf "%s", s
            bytes += length(s)
        }
    }' > "$name"
done

ls -lh logs
