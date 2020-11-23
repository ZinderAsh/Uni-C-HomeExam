#!/bin/bash

MIN_PORT=1024
MAX_PORT=65500

if [ "$#" -ne 1 ]; then
    echo "Please provide base port to the script. Value between [$MIN_PORT - $MAX_PORT]"
    exit
fi

if [ $1 -lt $MIN_PORT ] || [ $1 -gt $MAX_PORT ]; then
    echo "Port $1 is not in the range [$MIN_PORT, $MAX_PORT]"
    exit
fi

BASE_PORT=$1
MESSAGES_FILENAME="messages_2.txt"
LOG_DIR="./logs"
# Set to "valgrind" to run valgrind on nodes and server
VALGRIND="valgrind"

# Put logfiles here
if [ ! -d $LOG_DIR ]; then
    mkdir $LOG_DIR
fi

# Copy the messages for this scenario into the data file that 1 should read.
cp $MESSAGES_FILENAME "./data.txt"

# Run routing server C
$VALGRIND ./routing_server $BASE_PORT 9          &>"$LOG_DIR/routing_server_log.txt" &

# Wait for the central server to start. If you have to wait for more than 1 seconds you
# are probably doing something wrong. 
sleep 1

# Run all nodes
$VALGRIND ./node $BASE_PORT 1 2:4 4:2 16:8                 &>"$LOG_DIR/1_log.txt" &
$VALGRIND ./node $BASE_PORT 2 1:4 8:1                      &>"$LOG_DIR/2_log.txt" &
$VALGRIND ./node $BASE_PORT 4 1:2 16:5                     &>"$LOG_DIR/4_log.txt" &
$VALGRIND ./node $BASE_PORT 8 2:1 16:3 32:1                &>"$LOG_DIR/8_log.txt" &
$VALGRIND ./node $BASE_PORT 16 1:8 4:5 8:3 32:2 64:5 256:2 &>"$LOG_DIR/16_log.txt" &
$VALGRIND ./node $BASE_PORT 32 8:1 16:2 128:2 256:4        &>"$LOG_DIR/32_log.txt" &
$VALGRIND ./node $BASE_PORT 64 16:5 256:5                  &>"$LOG_DIR/64_log.txt" &
$VALGRIND ./node $BASE_PORT 128 32:2 256:3                 &>"$LOG_DIR/128_log.txt" &
$VALGRIND ./node $BASE_PORT 256 16:2 32:4 64:5 128:3       &>"$LOG_DIR/256_log.txt" &

# Terminate all processes in case of failure
trap "trap - SIGTERM && kill -- -$$" SIGINT SIGTERM

# Wait for processes to finish
echo "Waiting for processes to finish"
wait



