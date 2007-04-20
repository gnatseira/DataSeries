#
#  (c) Copyright 2005, Hewlett-Packard Development Company, LP
#
#  See the file named COPYING for license details
#

package BatchParallel::nettrace2ds;
use strict;
use vars '@ISA';
use File::Copy;
use File::Compare;

@ISA = qw/BatchParallel::common/;

sub new {
    my $class = shift;

    my $this = bless {}, $class;
    while (@_ > 0) {
	$_ = shift @_;
	if ($_ eq 'help') {
	    $this->usage();
	    exit(0);
	} elsif (/^((info)|(convert)|(lzf2bz2)|(freeup))$/o) {
	    die "already have a mode" if defined $this->{mode};
	    $this->{mode} = $_;
	} elsif (/^infodir=(\S+)$/o) {
	    $this->{infodir} = $1;
	} elsif (/^dsdir=(\S+)$/o) {
	    $this->{dsdir} = $1;
	} elsif (/^groupsize=(\d+)$/o) {
	    $this->{groupsize} = $1;
	} elsif (/^compress=(.+)$/o) {
	    $this->{compress} = $1;
	} elsif (/^extent-size=(\d+)$/o) {
	    $this->{extent_size} = $1;
	} elsif (/^record-start=(\d+)$/o) {
	    $this->{first_record_num} = $1;
	} elsif (/^freedir=(.+)$/o) {
	    $this->{freedir} = $1;
	} elsif (/^finished_before=(\d+)$/o) {
	    $this->{finished_before} = $1;
	} else {
	    die "unknown options specified for batch-parallel module $class: '@_'";
	}
    }
    die "Don't have a mode??"
	unless defined $this->{mode};
    if ($this->{mode} eq 'info' || $this->{mode} eq 'convert') {
	die "Need to define infodir" unless defined $this->{infodir};
	die "$this->{infodir} is not a directory" unless -d $this->{infodir};
    }
    if ($this->{mode} eq 'convert') {
	die "Need to defined record-start=#" 
	    unless defined $this->{first_record_num};
	unless (defined $this->{compress}) {
	    print "Using default compress mode of bz2\n";
	    $this->{compress} = 'bz2';
	}
	unless (defined $this->{extent_size}) {
	    print "Using default extent size of 67108864\n";
	    $this->{extent_size} = 67108864;
	}
	unless (defined $this->{dsdir}) {
	    print "Using default dsdir of infodir ($this->{infodir})\n";
	    $this->{dsdir} = $this->{infodir};
	}
	unless (defined $ENV{NAME_KEY_1} && defined $ENV{NAME_KEY_2}) {
	    print "Missing \$ENV{NAME_KEY_[12]}, trying to read from ~/.hash-keys.txt.gpg\n";
	    open(KEYS, "gpg --decrypt $ENV{HOME}/.hash-keys.txt.gpg |") 
		or die "can't run gpg: $!";
	    $_ = <KEYS>; chomp;
	    die "?? $_" unless /^setenv NAME_KEY_1 ([0-9a-f]{32,})$/o;
	    $ENV{NAME_KEY_1} = $1;
	    $_ = <KEYS>; chomp;
	    die "?? $_" unless /^setenv NAME_KEY_2 ([0-9a-f]{32,})$/o;
	    $ENV{NAME_KEY_2} = $1;
	    close(KEYS);
	}

    }
    die "Need to define freedir"
	if $this->{mode} eq 'freedir' && !defined $this->{freedir};
    return $this;
}

sub usage {
    print "batch-parallel nettrace2ds info infodir=<dir>\n";
    print "                           convert infodir=<dir> record-start=<num> [compress=<mode>] [extent-size=<size>] [dsdir=<dir>]\n";
    print "                           lzf2bz2 ???\n";
    print "                           freedir freedir=<dir>\n"
}

# PCAP file suffix is .pcap0, .pcap1, .pcap2, etc.;
# this rule is dictated by the "-C" option of tcpdump;
# which outputs files: xyz, xyz1, xyz2, etc.;
# we have to manually change xyz to xyz0 to have a consistent file name format;
sub file_is_source {
    my($this,$prefix,$fullpath,$filename) = @_;

    return 1 if $filename =~ /^endace\.\d+\.128MiB\.((lzf)|(zlib\d)|(bz2))$/o;
    return 1 if $filename =~ /\.pcap\d*$/o; 
    return 0;
}

sub find_things_to_build {
    my($this, @sources) = @_;

    if ($this->{mode} eq 'freeup') {
	$this->freeup(@sources);
	exit(0);
    }

    my ($source_count, @file_list) = $this->SUPER::find_things_to_build(@sources);
    
    if ($this->{mode} eq 'lzf2bz2') {
	my @ret = grep(/\.lzf$/o, @file_list);
	return (scalar @ret, @ret);
    }

    # now $this->{mode} = info|convert

    # auto-detect file type 
    # erf: files are named as "endace.<digits>.128MiB.<word>"
    # pcap: files are named as "<word>.pcap<digits>"
    my $is_erf = 0;
    my $file;
    foreach $file (@file_list) {
	if  ($file =~ /endace\.(\d+)\.128MiB.\w+$/o) { # we don't use /^ b/c $file includes path name
	    $is_erf = 1;
	    last;
	}
    }
    my $is_pcap = 0;
    foreach $file (@file_list) {
	if ($file =~ /\.pcap(\d*)/o) {
	    $is_pcap = 1;
	    last;
	}
    }
    if (($is_erf==1 && $is_pcap==1) || ($is_erf==0 && $is_pcap==0)) {
	die "Directory should contain only ERF or PCAP files.\n";
    }

    if ($is_erf == 1) { $this->{file_type} = 'erf'; }
    else { $this->{file_type} = 'pcap'; }

    my %num_to_file;
    if ($this->{file_type} eq 'erf') {
	map { /endace\.(\d+)\.128MiB.\w+$/o || die "huh $_";
	      die "Duplicate number $1 from $_ and $num_to_file{$1}"
		  if defined $num_to_file{$1};
	      $num_to_file{$1} = $_; } @file_list;
    } else {
	map { /.pcap(\d*)/o || die "huh $_"; # PCAP file name format is *.pcap0, *.pcap1, *.pcap2, etc.
	      die "Duplicate number $1 from $_ and $num_to_file{$1}"
		  if defined $num_to_file{$1};
	      $num_to_file{$1} = $_; } @file_list;
    }

    $this->{groupsize} = int(sqrt($source_count)) unless defined $this->{groupsize};
    
    my @nums = sort { $a <=> $b } keys %num_to_file;
    
    my @ret;
    my $group_count = 0;
    my $running_record_num = $this->{first_record_num};
    $this->{finished_before} ||= 0;
    for(my $i=0; $i < @nums; $i += $this->{groupsize}) {
	my @group;
	my $first_num = $nums[$i];
	my $last_num = $nums[$i+$this->{groupsize}-1] || $nums[@nums-1];
	for(my $j = $first_num; $j <= $last_num; ++$j) {
	    my $k;
	    if ($this->{file_type} eq 'erf') { $k = sprintf("%06d", $j); }
	    else { $k = $j; }
	    die "missing num $k" unless defined $num_to_file{$k};
	    push(@group, $num_to_file{$k});
	    delete $num_to_file{$k};
	}
	++$group_count;
	my $infoname = "$this->{infodir}/$first_num-$last_num\.info";
	if ($this->{mode} eq 'info') {
	    push(@ret, { 'first' => $first_num, 'last' => $last_num, 'files' => \@group,
			 'infoname' => $infoname, })
		if ! -f $infoname || $this->file_older($infoname, @group);
	} elsif ($this->{mode} eq 'convert') {
	    open(INFO, $infoname)
		or die "Unable to open $infoname: $!";
	    while(<INFO>) {
		last if /^first_record_id: 0$/o;
	    }
	    die "can't find first_record_id in $infoname"
		unless defined $_;
	    $_ = <INFO>; chomp;
	    die "?? '$_'"
		unless /^last_record_id \(inclusive\): (\d+)$/o;
	    my $record_count = $1 + 1;
	    my $dsname = "$this->{dsdir}/$first_num-$last_num\.ds";
	    push(@ret, { 'first' => $first_num, 'last' => $last_num, 'files' => \@group,
			 'dsname' => $dsname, 'record_start' => $running_record_num,
			 'record_count' => $record_count })
		if $last_num >= $this->{finished_before} && 
		(! -f $dsname || $this->file_older($dsname, @group));
	    $running_record_num += $record_count;
	} else { 
	    die "??";
	}
    }
    if ($this->{mode} eq 'convert') {
	open(LAST_RUNNING, ">$this->{infodir}/last-running")
	    or die "Can't open $this->{infodir}/last-running for write: $!";
	print LAST_RUNNING $running_record_num,"\n";
	close(LAST_RUNNING);
	print "Last running record num: $running_record_num\n";
    }
    return ($group_count, @ret);
}

sub determine_things_to_build {
    return map { $_->[1] } @{$_[1]};
}

sub rebuild_thing_do {
    my($this, $thing_info) = @_;

    my $cmd;
    if ($this->{mode} eq 'convert') {
	die "??" unless defined $thing_info->{dsname};
	die "??" unless defined $thing_info->{record_start} && defined $thing_info->{record_count};
	if ($this->{file_type} eq 'erf') {
	    $cmd = "nettrace2ds --convert-erf --compress-$this->{compress} --extent-size=$this->{extent_size} $thing_info->{record_start} $thing_info->{record_count} $thing_info->{dsname}-new " . join(" ", @{$thing_info->{files}}) . " >$thing_info->{dsname}-log 2>&1";
	} elsif ($this->{file_type} eq 'pcap') {
	    $cmd = "nettrace2ds --convert-pcap " . join(" ", @{$thing_info->{files}});
	} else {
	    die "Code shouldn't reach  here, file type is $this->{file_type} but should be erf or pcap\n";
	}
	unlink("$thing_info->{dsname}-fail");
	print "Creating $thing_info->{dsname}...\n";
    } elsif ($this->{mode} eq 'info') {
	die "??" unless defined $thing_info->{infoname};
	if ($this->{file_type} eq 'erf') {
	    $cmd = "nettrace2ds --info-erf " . join(" ", @{$thing_info->{files}}) . " >$thing_info->{infoname}-new 2>$thing_info->{infoname}-log";
	} elsif ($this->{file_type} eq 'pcap') {
	    $cmd = "nettrace2ds --info-pcap " . join(" ", @{$thing_info->{files}}) . " >$thing_info->{infoname}-new 2>$thing_info->{infoname}-log";
	} else {
	    die "Code shouldn't reach here, file type is $this->{file_type} but should be erf or pcap\n";
	}
	unlink("$thing_info->{infoname}-fail");
	print "Creating $thing_info->{infoname}...\n";
    } elsif ($this->{mode} eq 'lzf2bz2') {
	die "??" unless -f $thing_info;
	my $out = $thing_info;
	$out =~ s/\.lzf$/.bz2/o || die "??";
	if (-f "$out-new") {
	    print "$out-new already exists, skipping directly to comparison...\n";
	} else {
	    $cmd = "nettrace2ds --recompress-bz2 $thing_info $out-new";
	    print "$cmd\n";
	    my $ret = system($cmd);
	    exit(1) unless $ret == 0;
	}
	system("sync") == 0 or die "sync failed";
	$cmd = "nettrace2ds --check-erf-equal $thing_info $out-new";
    } else {
	die "??";
    }
    die "??" unless defined $cmd;
    my $ret = system($cmd);
    exit(1) unless $ret == 0;
    exit(0);
}

sub rebuild_thing_success {
    my($this, $thing_info) = @_;

    if ($this->{mode} eq 'convert') {
	die "??" unless defined $thing_info->{dsname};
	die "huh" unless -f "$thing_info->{dsname}-new";
	rename("$thing_info->{dsname}-new",$thing_info->{dsname})
	    or die "rename failed: $!";
	print "Successfully created $thing_info->{dsname}\n";
    } elsif ($this->{mode} eq 'info') {
	die "file $thing_info->{infoname}-new does not exist" 
	    unless -f "$thing_info->{infoname}-new";
	rename("$thing_info->{infoname}-new",$thing_info->{infoname})
	    or die "rename failed: $!";
	print "Successfully created $thing_info->{infoname}\n";
    } elsif ($this->{mode} eq 'lzf2bz2') {
	my $out = $thing_info;
	$out =~ s/\.lzf$/.bz2/o || die "??";
	die "huh" unless -f "$out-new";
	rename("$out-new",$out) or die "rename failed: $!";
	my $insize = -s $thing_info;
	my $outsize = -s $out;
	$this->{cumulative_count} ||= 0;
	$this->{cumulative_savings} ||= 0;
	$this->{cumulative_savings} += ($insize - $outsize);
	++$this->{cumulative_count};
	my $tmp = $thing_info;
	$tmp =~ s!^.*/!!o;
	printf "Saved %.2f MiB on $tmp, %.2fMiB cumulative, %.2fMiB/file\n",
	    ($insize - $outsize)/(1024*1024), $this->{cumulative_savings}/(1024*1024),
	    $this->{cumulative_savings}/(1024*1024*$this->{cumulative_count});
	unlink($thing_info) or die "unlink failed: $!";
    } else {
	die "??";
    }
}

sub rebuild_thing_fail {
    my($this, $thing_info) = @_;

    if ($this->{mode} eq 'convert') {
	die "??" unless defined $thing_info->{dsname};
	print "Failed to create $thing_info->{dsname}\n";
	rename("$thing_info->{dsname}-log","$thing_info->{dsname}-fail")
	    or die "rename failed: $!";
    } elsif ($this->{mode} eq 'info') {
	print "Failed to create $thing_info->{infoname}\n";
	rename("$thing_info->{infoname}-log","$thing_info->{infoname}-fail")
	    or die "rename failed: $!";
    } elsif ($this->{mode} eq 'lzf2bz2') {
	my $out = $thing_info;
	$out =~ s/\.lzf$/.bz2/o || die "??";
	die "huh" unless -f "$out-new";
	print "Failed to compare $thing_info to $out-new, removing $out-new\n";
	unlink("$out-new");
    } else {
	die "??";
    }

}

sub rebuild_thing_message {
    # die "unimplemented";
    my($this, $thing_info) = @_;
    print("rebuilding files: ");
    for (my $i = 0; $i < @{$thing_info->{files}}; ++$i) {
	print "$thing_info->{files}[$i] ";
    }
    print "\n";
}

sub df {
    my($dir) = @_;
    
    open(DF, "df -k $dir | ") or die "Can't run df: $!";
    $_ = <DF>; chomp;
    die "?? '$_'" unless /^Filesystem\s+1K-blocks\s+Used\s+Available\s+/o;
    $_ = <DF>; chomp;
    die "?? '$_'" unless /^\S+\s+\d+\s+\d+\s+(\d+)\s+\d+\%\s+\S+/o;
    close(DF);
    return $1/1024; # MiB
}

sub freeup {
    my($this, @todirs) = @_;

    @todirs = grep($_ ne $this->{freedir}, @todirs);
    opendir(DIR, $this->{freedir})
	or die "No opendir $this->{freedir}: $!";
    $|=1;
    my @pairs;
    while(my $file = readdir(DIR)) {
      retry:
	last if @todirs == 0;
	next unless -f "$this->{freedir}/$file";
	next if $file =~ /-new$/o || $file =~ /.lzf$/o;
	my($from,$to) = ("$this->{freedir}/$file","$todirs[0]/$file");

	if (-f $to) {
	    push(@pairs,[$from,$to]);
	    next;
	}
	my $free = df($todirs[0]);
	if ($free < 500) {
	    print "only $free MiB available on $todirs[0], skipping\n";
	    shift @todirs;
	    goto retry;
	}
	die "Huh $to exists??" 
	    if -f $to;
	die "?? $to" if $to =~ m!^$this->{freedir}/!o;
	print "cp $from $to; $free MiB free\n";
	if (copy($from,$to)) {
	    push(@pairs,[$from,$to]);
	} else {
	    print "$todirs[0] copy failed??\n";
	    unlink($to);
	    shift @todirs;
	    redo;
	}
    }
    print "sync\n";
    system("sync") == 0 or die "sync failed";
    foreach my $pair (@pairs) {
	my($from,$to) = @$pair;
	die "?? $from" unless $from =~ m!^$this->{freedir}/!o;
        print "cmp $from $to\n";
	die "compare failed" unless compare($from,$to) == 0;
	print "rm $from\n";
	unlink($from);
    }
    exit(0);
}

1;

