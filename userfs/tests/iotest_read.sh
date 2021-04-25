#!/bin/sh

./io_test sr 500M 32K 1

./io_test sr 500M 32K 2

./io_test sr 500M 32K 4

./io_test sr 500M 32K 6

./io_test sr 500M 32K 8

./io_test sr 500M 32K 10

./io_test sr 500M 32K 12

./io_test sr 500M 32K 16

./io_test sr 500M 32K 20

./io_test rr 500M 32K 1

./io_test rr 500M 32K 2

./io_test rr 500M 32K 4

./io_test rr 500M 32K 6

./io_test rr 500M 32K 8

./io_test rr 500M 32K 10

./io_test rr 500M 32K 12

./io_test rr 500M 32K 16

./io_test rr 500M 32K 20
