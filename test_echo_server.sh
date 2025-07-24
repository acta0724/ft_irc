#!/bin/bash

# Start the server in the background
echo "Starting server..."
make run &
SERVER_PID=$!

# Cleanup function to ensure the server is killed on script exit
cleanup() {
    echo "Stopping server (PID: $SERVER_PID)..."
    # Kill the entire process group associated with the server
    kill -- -$SERVER_PID
}

# Set the trap to call the cleanup function on EXIT
trap cleanup EXIT

# Wait a moment for the server to initialize
echo "Waiting for server to start..."
sleep 1

# Run the test
echo "Running tests..."
if python3 test_echo.py; then
    echo "✅ Test passed!"
    exit 0
else
    echo "❌ Test failed!"
    exit 1
fi
