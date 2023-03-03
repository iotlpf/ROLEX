#!/usr/bin/env bash
# this script will sync the project to the remote server

user="wjq"
target=("val08" "val09")

for machine in ${target[*]}
do
      rsync -i -rtuv \
            $PWD/../core $PWD/../tests $PWD/../examples $PWD/../benchs $PWD/../CMakeLists.txt \
            ${user}@${machine}:/home/${user}/rib
done
