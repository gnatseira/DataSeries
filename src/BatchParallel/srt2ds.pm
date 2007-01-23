#
#  (c) Copyright 2005, Hewlett-Packard Development Company, LP
#
#  See the file named COPYING for license details
#

package BatchParallel::srt2ds;

@ISA = qw/BatchParallel::common/;

sub new {
    my $class = shift;

    my $this = bless {}, $class;
    while (@_ > 0) {
	$_ = shift @_;
	if ($_ eq 'help') {
	    $this->usage();
	    exit(0);
	} elsif (/^compress=(.+)$/o) {
	    die "Already have a filter" if defined $this->{compress};
	    $this->{compress} = $1;
	} else {
	    die "unknown options specified for batch-parallel module $class: '@_'";
	}
    }
    return $this;
}

sub usage {
    print "batch-parallel srt2ds [compress={bz2,lzf,gz,lzo}] -- file/directory...\n";
}

sub file_is_source {
    my($this,$prefix,$fullpath,$filename) = @_;

    return 1 if $fullpath =~ /\.srt(|(\.bz2)|(\.gz))$/o;
    return 0;
}

sub destination_file {
    my($this,$prefix,$fullpath) = @_;

    $fullpath =~ s/\.srt(|(\.bz2)|(\.gz))$/.ds/o;
    return $fullpath;
}

sub rebuild {
    my($this,$prefix,$fullpath,$destpath) = @_;

    my $compress = '';
    $compress = "--compress-$this->{compress}";
    my $command = "srt2ds $compress $fullpath $destpath";
    print "$command\n";
    return system($command) == 0;
}

1;
