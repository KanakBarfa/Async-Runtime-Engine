#!/usr/bin/env bash

# Exit immediately if a command exits with a non-zero status
set -e

# Increase file descriptor limit for high connection benchmarks
ulimit -n 10000 || true

PORT=18080
DURATION=5
THREADS=4
PAYLOAD=64

# Locate binaries
CLIENT="./build/benchmarks/benchmark_client"
SERVERS=(
    "./build/benchmarks/server_thread_per_connection"
    "./build/benchmarks/server_epoll"
    "./build/benchmarks/server_async_engine"
    "./build/benchmarks/server_async_engine_fixed"
    "./build/benchmarks/server_coroutine"
)
SERVER_NAMES=(
    "Thread-per-Connection"
    "Event-Driven Epoll"
    "async_engine (Sender/Receiver)"
    "async_engine (Fixed Buffers)"
    "Coroutine (exec::task)"
)

CONNS_SWEEP=(200 500 1000 2000 4000)

# Output result file
RESULTS_FILE="./benchmarks/results.md"
echo "# Benchmark Results & Evaluation" > "$RESULTS_FILE"
echo "Generated on $(date)" >> "$RESULTS_FILE"
echo "" >> "$RESULTS_FILE"
echo "## Configuration" >> "$RESULTS_FILE"
echo "- **Duration:** ${DURATION}s" >> "$RESULTS_FILE"
echo "- **Threads:** ${THREADS}" >> "$RESULTS_FILE"
echo "- **Payload Size:** ${PAYLOAD} bytes" >> "$RESULTS_FILE"
echo "" >> "$RESULTS_FILE"
echo "## Scaling Sweep Results" >> "$RESULTS_FILE"
echo "" >> "$RESULTS_FILE"
echo "| Connections | Server | Throughput (req/sec) | Latency p50 (us) | Latency p99 (us) | Latency Avg (us) |" >> "$RESULTS_FILE"
echo "|---|---|---|---|---|---|" >> "$RESULTS_FILE"

echo "==========================================="
echo "Starting Benchmarks Sweep"
echo "==========================================="

for CONNS in "${CONNS_SWEEP[@]}"; do
    echo "==========================================="
    echo "Running sweep with $CONNS connections"
    echo "==========================================="

    for i in "${!SERVERS[@]}"; do
        SERVER="${SERVERS[$i]}"
        NAME="${SERVER_NAMES[$i]}"

        echo "-------------------------------------------"
        echo "Testing Server: $NAME with $CONNS conns"
        echo "-------------------------------------------"

        # Start server in background
        $SERVER $PORT > /dev/null 2>&1 &
        SERVER_PID=$!

        # Wait for server to bind
        sleep 1

        # Run benchmark client and capture output
        set +e
        CLIENT_OUTPUT=$($CLIENT --ip 127.0.0.1 --port $PORT --conns $CONNS --threads $THREADS --payload $PAYLOAD --duration $DURATION)
        CLIENT_STATUS=$?
        set -e

        # Terminate server
        kill $SERVER_PID || true
        sleep 0.5
        kill -9 $SERVER_PID >/dev/null 2>&1 || true
        wait $SERVER_PID 2>/dev/null || true

        if [ $CLIENT_STATUS -ne 0 ]; then
            echo "Client failed to run against $NAME with $CONNS conns"
            echo "| $CONNS | $NAME | FAILED | | | |" >> "$RESULTS_FILE"
            continue
        fi

        # Parse metrics from output
        THROUGHPUT=$(echo "$CLIENT_OUTPUT" | grep "Throughput:" | awk '{print $2}')
        P50=$(echo "$CLIENT_OUTPUT" | grep "Latency p50:" | awk '{print $3}')
        P99=$(echo "$CLIENT_OUTPUT" | grep "Latency p99:" | awk '{print $3}')
        AVG=$(echo "$CLIENT_OUTPUT" | grep "Latency Average:" | awk '{print $3}')

        echo "Results for $NAME ($CONNS conns):"
        echo "  Throughput: $THROUGHPUT req/sec"
        echo "  Latency p50: $P50 us"
        echo "  Latency p99: $P99 us"
        echo "  Latency Avg: $AVG us"

        echo "| $CONNS | $NAME | $THROUGHPUT | $P50 | $P99 | $AVG |" >> "$RESULTS_FILE"
    done
done

echo "" >> "$RESULTS_FILE"
echo "==========================================="
echo "Benchmarks Completed! Results written to $RESULTS_FILE"
echo "==========================================="
