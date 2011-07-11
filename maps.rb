#!/usr/bin/ruby
# -*- coding: utf-8 -*-
while line = gets
  line.chomp!
  if line =~ /^([0-9a-fA-F]+)-([0-9a-fA-F]+)\s+([r-])([w-])([x-])([ps-])\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+):([0-9a-fA-F]+)\s+([0-9]+)\s*(.+)/
    first = sprintf("%016x", $1.to_i(16))
    last  = sprintf("%016x", $2.to_i(16))
    readable = $3
    writable = $4
    executable = $5
    shared = $6
    offset = sprintf("%016x", $7.to_i(16))
    device_major = $8
    device_minor = $9
    inode = sprintf("%010d", $10.to_i(10))
    mapped = $11.strip
    puts "#{first}-#{last} #{readable}#{writable}#{executable}#{shared} #{offset} #{device_major}:#{device_minor} #{inode}"
    puts "#{mapped}"
  else
    warn "BAD: " + line
  end
end
