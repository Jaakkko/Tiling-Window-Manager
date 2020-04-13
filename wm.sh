#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" > /dev/null 2>&1 && pwd )"

success=true
while true
do
  if [ $success == true ]
  then
    cp $DIR/cmake-build-debug/WM $DIR/cmake-build-debug/WM_tmp
    $DIR/cmake-build-debug/WM
    code=$?
    if [ $code -eq 130 ]; then break; fi
    [[ $code -eq 0 ]] && success=true || success=false
    if [ $success == true ]; then
      cp $DIR/cmake-build-debug/WM_tmp $DIR/cmake-build-debug/WM_backup
    fi
  else
    $DIR/cmake-build-debug/WM_backup && success=true || break
    if [ $? -eq 130 ]; then break; fi
  fi
done
