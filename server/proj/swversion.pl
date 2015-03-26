#! /usr/bin/perl;

open (OUTPUTFILE, ">../src/swversion.c") || die ("Could not open file");

($sec,$min,$hour,$day,$mon,$year) = localtime();
$mon++;$year += 1900;
$data_now = sprintf("%d-%d-%d %d:%d:%d",$year,$mon,$day,$hour,$min,$sec);

print OUTPUTFILE ("char gSwversionTime[] = \"" . $data_now . "\";\n");

$line = `whoami`;
chomp($line);
print OUTPUTFILE ("char gSwversionBuilder[] = \"" . $line . "\";\n");


