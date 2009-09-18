#!/bin/bash

. options
. functions

server_num=2
server_id_1=00
server1_addr=127.0.0.1:1025:2
server_id_2=ab
server2_addr=127.0.0.1:1030:2
file_id_1=44

tmpdir=`prepare_root`

log_file=$tmpdir/log
tmp_file=$tmpdir/test_1_tmp
res_file=$tmpdir/test_1_res

trap kill_servers EXIT

echo "rw_test ($tmpdir):"

generate_random_file $tmp_file

function test_1 {
    # ***************************************
    # * 1 server - 1 client                 *
    # ***************************************
    log_start "1 server - 1 client"

    #start first server
    start_server $server1_addr 0 "$server_opt -i $server_id_1"
    serever1_pid=last_pid

    write_data 0 0 0 $server1_addr $tmp_file $file_id_1
    read_data 1 0 0 $server1_addr $res_file $file_id_1
    cmp_files $tmp_file $res_file

    #clean  
    kill_servers
    rm $res_file*

    print_passed
}

function test_2 {
    # ***************************************
    # * 1 server - 1 client                 *
    # *  send file in 2 transactions        *
    # ***************************************
    log_start "1 server - 1 client"
    echo " send file in 2 transactions"

    total_size=$(stat --printf="%s" $tmp_file)
    first_trans_size=$(($total_size/2))
    second_trans_size=$(($first_trans_size+($total_size%2)))

    #start first server
    start_server $server1_addr 0 "$server_opt -i $server_id_1"
    serever1_pid=last_pid

    write_data 0 $first_trans_size 0 $server1_addr $tmp_file $file_id_1
    write_data 0 $second_trans_size $first_trans_size $server1_addr $tmp_file $file_id_1
    read_data 1 0 0 $server1_addr $res_file $file_id_1
    cmp_files $tmp_file $res_file

    #clean  
    kill_servers
    rm $res_file*

    print_passed
}

function test_3 {
    # ***************************************
    # * 2 server - 1 client                 *
    # *  send request to old first  server  *
    # ***************************************
    log_start "2 server - 1 client"
    echo " send request to old first  server"

    #start first server
    start_server $server1_addr 0 "$server_opt -i $server_id_1"
    serever1_pid=last_pid

    #start second server
    start_server $server2_addr 1 "$server_opt -i $server_id_2 -r $server1_addr"
    serever2_pid=last_pid

    write_data 0 0 0 $server1_addr $tmp_file $file_id_1
    read_data 1 0 0 $server1_addr $res_file $file_id_1
    cmp_files $tmp_file $res_file

    #clean  
    kill_servers
    rm $res_file*

    print_passed
}

function test_4 {
    # ***************************************
    # * 2 server - 1 client                 *
    # *  send request to new second server  *
    # ***************************************
    log_start "2 server - 1 client"
    echo " send request to new second server"

    #start first server
    start_server $server1_addr 0 "$server_opt -i $server_id_1"
    serever1_pid=last_pid

    #start second server
    start_server $server2_addr 1 "$server_opt -i $server_id_2 -r $server1_addr"
    serever2_pid=last_pid

    write_data 0 0 0 $server1_addr $tmp_file $file_id_1
    read_data 1 0 0 $server2_addr $res_file $file_id_1
    cmp_files $tmp_file $res_file

    #clean  
    kill_servers
    rm $res_file*

    print_passed
}

function test_5 {
    # ***************************************
    # * 2 server - 1 client - 2 copies      *
    # *  send request to new second server  *
    # ***************************************
    log_start "2 server - 1 client - 2 copies"
    echo " send request to new second server"

    #start first server
    start_server $server1_addr 0 "$server_opt -i $server_id_1" 1
    serever1_pid=last_pid

    #start second server
    start_server $server2_addr 1 "$server_opt -i $server_id_2 -r $server1_addr" 1
    serever2_pid=last_pid

    write_data 0 0 0 $server1_addr $tmp_file -1 1
    read_data 1 0 0 $server2_addr $res_file -1 1
    cmp_files $tmp_file $res_file
    rm $res_file*

    sleep 2
    kill $serever1_pid
    # get data from second server
    read_data 2 0 0 $server2_addr $res_file -1 1
    cmp_files $tmp_file $res_file

    #clean  
    kill_servers
    rm -f $res_file*

    print_passed

}

function test_6 {
    # ***************************************
    # * 1 server - 1 client                 *
    # *  kill first server and              *
    # *  send request to second server      *
    # *  (simple join test)                 *
    # ***************************************
    log_start "2 server - 1 client"
    echo " kill first server and"
    echo " send request to second server"
    echo " (simple join test)"

    #start first server
    start_server $server1_addr 0 "$server_opt -i $server_id_1"
    serever1_pid=last_pid

    write_data 0 0 0 $server1_addr $tmp_file $file_id_1

    #start second server
    start_server $server2_addr 1 "$server_opt -i $server_id_2 -r $server1_addr"
    serever2_pid=last_pid

    read_data 1 0 0 $server2_addr $res_file $file_id_1
    cmp_files $tmp_file $res_file

    #clean  
    kill_servers
    rm $res_file*

    print_passed

    rm -rf $tmpdir
}

test_1
test_2
test_3
test_4
test_5
test_6