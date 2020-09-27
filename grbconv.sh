#!/bin/env bash
rm $1.fab
rm $1.gtp
rm $1.gbp
mv $1.drl $1.txt
mv $1.gm1 $1.gko
zip $1 *
