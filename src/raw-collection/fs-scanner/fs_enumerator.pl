#!/usr/bin/perl -w
#   (c) Copyright 2007, Hewlett-Packard Development Company, LP
#
#   See the file named COPYING for license details
#
# Description: File system directory scanner

#This is MESSY CODE Designed to scan a set of directories and store
#all of the metadata in rotating logfiles defined by $outFileSpec If
#one of the directories scanned is /, the last function silently
#prunes proc, dev, sys subtrees because their metadata does not seem
#to be valid and screws up the file system size totals 
#
# TODO: clean this up ;)
# 
#TODO: Make this work with BatchParallel, with configurable
#compression, and with configurable log rotation size

use File::Find;
use MIME::Base64;
use strict;

$|=1;
my $maxDepth = 1;
my $curDepth = 0;
my $entryRotateCount = 1000000;
usage() unless @ARGV > 0 || $ARGV[0] =~ /^-h/o;
usage("outfile-prefix should not exist") if -f $ARGV[0];

sub usage {
    print "Usage: $0 outfile-prefix (dir|file)...\n";
    print "outfile-prefix is the prefix for all output files\n";
    print "dir is one or more directories to be scanned in order\n";
    print "file is one or more files that contain a list of directories to be scanned\n";
    die @_ if @_ > 0;
    exit(0);
}
my $outFileSpec = shift @ARGV;
my @pending = @ARGV;

# print "argv[1] is @dirList and ARGV[0] is $outFileSpec\n";
# print "startdir 0 is $dirList[0] 1 is $dirList[1]\n";

my $processed_dir_count = 0;
my $outFileSpecComplete;
my $outFileEntryCount = 0;
my $outFileCurCount = 0;

my $baseDirectory;
my $totalSize = 0;
my $totalCount = 0;
my $curSize = 0;

while (@pending > 0) {
    my $thing = shift @pending;
    my $logname = $thing; 
    $logname =~ s!/!_!go;
    $logname = "$outFileSpec-$logname";
    if (-f "$logname-done") {
	print "Already scanned $thing, skipping.\n";
	next;
    }
    waitFor($thing);
    if (-f $thing) {
 	open (FILEOFDIRS, $thing) || die "couldn't open $thing ($!)";
 	while (<FILEOFDIRS>) {
 	    chomp;
	    push(@pending, $_);
	}
	close(FILEOFDIRS);
    } elsif (-d $thing) {
	$outFileSpecComplete = $logname;
	die "??" if -e "$logname-done";

	print "logging to $outFileSpecComplete\n";
	$outFileCurCount = 0;
	rotateLogs();

	$totalSize = $totalCount = $curSize = $outFileEntryCount = 0;
	$baseDirectory = $thing;
	find(\&wanted, $thing);
	print STDERR "done with \#$processed_dir_count: $thing\n";
	++$processed_dir_count;
	close(OUTFILE);
	open(FOO, ">$outFileSpecComplete-done")
	    or die "boo";
	close(FOO);
    } else {
	warn "WARNING: neither a directory nor file be, so skipping: $thing";
    }
}

sub waitFor {
    my($what) = @_;

    print "waitFor $what: ";
    for(my $i = 0; $i < 300; ++$i) {
	last if -d $what || -f $what;
	print ".";
	sleep(1);
    }
    if (-d $what || -f $what) {
	print "ok.\n";
    } else {
	print "failed, waited 300 seconds\n";
    }
}

sub rotateLogs {
    close(OUTFILE);
    my $compressor = "gzip -1 -c";
    my $outFileName = "| $compressor > $outFileSpecComplete-$outFileCurCount.gz";
    open(OUTFILE, $outFileName) or die "couldn't open $outFileName ($!)";
    $outFileCurCount++;
}

sub wanted {
    #print "$_\n";
#    my ($dev,$ino,$mode,$nlink,$uid,$gid,$rdev,$size,
#     $atime,$mtime,$ctime,$blksize,$blocks) = stat $_;
    my @statbits = lstat $_;
    die "?? $File::Find::dir $_" unless @statbits == 13;
    my $b64Name = encode_base64($_, "")|| die "can't encode $_ because $!";
    my $b64NameLength = length $b64Name;
    my $b64Dir = encode_base64($File::Find::dir, "")|| die "can't encode $File::Find::dir because $!";
    my $b64DirLength = length $b64Dir;
    my $printString = join(" ",$b64DirLength,$b64NameLength, $b64Dir, $b64Name, @statbits);

    my $size = $statbits[7];
    print OUTFILE "$printString\n";
    #print "$File::Find::dir $_ $dev $ino $mode $nlink $uid $gid $rdev $size $atime $mtime $ctime $blksize $blocks\n";
    ++$totalCount;
    if (-f $_) {
	$curSize += $size;
	if ($curSize > (500*1024*1024)) {
	    $totalSize += $curSize;
	    $curSize = 0;
	    my $printCount = $totalSize/(1024*1024*1024);
	    printf STDERR "%.3f GiB, %.3f Mfiles; $File::Find::name\n",
	       $totalSize/(1024*1024*1024), $totalCount/(1000*1000);
	}
    }
    $outFileEntryCount++;
    if ($outFileEntryCount > $entryRotateCount) {
	#Rotate
	rotateLogs();
	$outFileEntryCount = 0;
    }
    #Stuff to skip here -----
    if ($File::Find::dir eq $baseDirectory) {
	if ($File::Find::dir eq '/' &&
	    /^proc$/o || /^dev$/o || /^sys$/o) {
	    print STDERR "Prune -- /{proc,dev,sys}\n";
	    $File::Find::prune = 1;
	} elsif (/^\.snapshot$/o) {
	    print STDERR "Prune -- netapp .snapshot directory\n";
	    $File::Find::prune = 1;
	}
    }
}
