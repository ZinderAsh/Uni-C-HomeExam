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
MESSAGES_FILENAME="messages_1.txt"
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
$VALGRIND ./routing_server $BASE_PORT 8          &>"$LOG_DIR/routing_server_log.txt" &

# Wait for the central server to start. If you have to wait for more than 1 seconds you
# are probably doing something wrong. 
sleep 1

# Run all nodes
$VALGRIND ./node $BASE_PORT 1 11:2 103:6         &>"$LOG_DIR/1_log.txt" &
$VALGRIND ./node $BASE_PORT 11 1:2 13:7 19:2     &>"$LOG_DIR/11_log.txt" &
$VALGRIND ./node $BASE_PORT 13 11:7 17:3 101:4   &>"$LOG_DIR/13_log.txt" &
$VALGRIND ./node $BASE_PORT 17 13:3 107:2        &>"$LOG_DIR/17_log.txt" &
$VALGRIND ./node $BASE_PORT 19 11:2 101:2 103:1  &>"$LOG_DIR/19_log.txt" &
$VALGRIND ./node $BASE_PORT 101 13:4 19:2 107:2  &>"$LOG_DIR/101_log.txt" &
$VALGRIND ./node $BASE_PORT 103 1:6 19:1 107:4   &>"$LOG_DIR/103_log.txt" &
$VALGRIND ./node $BASE_PORT 107 17:2 101:2 103:4 &>"$LOG_DIR/107_log.txt" &

# Terminate all processes in case of failure
trap "trap - SIGTERM && kill -- -$$" SIGINT SIGTERM

# Wait for processes to finish
echo "Waiting for processes to finish"
wait
exit 0

