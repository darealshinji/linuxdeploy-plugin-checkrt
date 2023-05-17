#! /usr/bin/env bash
set -e

script="linuxdeploy-plugin-checkrt.sh"

files="checkrt.c
COPYING
COPYING3
COPYING.libgcc
COPYING.libstdc++
COPYING.RUNTIME
exec.c"

rm -f $script

# script header
cat << EOL > $script
#! /usr/bin/env bash
# This script was automatically generated!

set -e

EOL

# files
echo "save_files() {" >> $script

for f in $files ; do
    echo "    cat > "$f" << \__EOF__" >> $script
    cat $f >> $script
    echo -e "\n__EOF__\n" >> $script
done

cat << EOL >> $script
}
# save_files() end

EOL

# main part
cat template.sh >> $script
chmod a+x $script
