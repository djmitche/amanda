# Copyright (c) 2014 Zmanda, Inc.  All Rights Reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
#
# Contact information: Zmanda Inc, 465 S. Mathilda Ave., Suite 300
# Sunnyvale, CA 94086, USA, or: http://www.zmanda.com

use Test::More;
use File::Path;
use strict;
use warnings;

use lib '@amperldir@';
use Installcheck;
use Installcheck::Dumpcache;
use Installcheck::Config;
use Amanda::Paths;
use Amanda::Device qw( :constants );
use Amanda::Debug;
use Amanda::MainLoop;
use Amanda::Config qw( :init :getconf config_dir_relative );
use Amanda::Changer;

eval 'use Installcheck::Rest;';
if ($@) {
    plan skip_all => "Can't load Installcheck::Rest: $@";
    exit 1;
}

# set up debugging so debug output doesn't interfere with test results
Amanda::Debug::dbopen("installcheck");
Installcheck::log_test_output();

# and disable Debug's die() and warn() overrides
Amanda::Debug::disable_die_override();

my $rest = Installcheck::Rest->new();
if ($rest->{'error'}) {
   plan skip_all => "Can't start JSON Rest server: $rest->{'error'}: see " . Amanda::Debug::dbfn();
   exit 1;
}
plan tests => 1;

my $reply;

my $amperldir = $Amanda::Paths::amperldir;
my $testconf;

$testconf = Installcheck::Run::setup();
$testconf->add_dle("localhost /home installcheck-test");
$testconf->add_dle(<<EOF);
localhost /home-incronly {
    installcheck-test
}
localhost /etc {
    installcheck-test
}
EOF

$testconf->write();

config_init($CONFIG_INIT_EXPLICIT_NAME, "TESTCONF");
my $diskfile = Amanda::Config::config_dir_relative(getconf($CNF_DISKFILE));
my $infodir = getconf($CNF_INFOFILE);

#CODE 28* 123
$reply = $rest->post("http://localhost:5001/amanda/v1.0/configs/TESTCONF/amcheck","");
foreach my $message (@{$reply->{'body'}}) {
    if (defined $message and defined $message->{'message'}) {
	$message->{'message'} =~ s/^NOTE: host info dir .*$/NOTE: host info dir/;
	$message->{'message'} =~ s/^NOTE: index dir .*$/NOTE: index dir/;
	$message->{'message'} =~ s/^Holding disk .*$/Holding disk : disk space available, using as requested/;
	$message->{'message'} =~ s/^Server check took .*$/Server check took 1.00 seconds/;
	$message->{'message'} =~ s/^Client check: 1 host checked in \d+.\d+ seconds.  1 problem found.$/Client check: 1 host checked in 1.00 seconds.  1 problem found./;
	$message->{'message'} =~ s/^\(brought to you by Amanda .*$/(brought to you by Amanda x.y.z)/;
    }
}
is_deeply (Installcheck::Rest::remove_source_line($reply),
    { body =>
        [ {	'source_filename' => "amcheck.c",
		'severity' => '16',
		'message' => "Amanda Tape Server Host Check",
		'code' => '2800027'
	  },
          {	'source_filename' => "amcheck.c",
		'severity' => '16',
		'message' => "-----------------------------",
		'code' => '2800028'
	  },
          {	'source_filename' => "amcheck.c",
		'severity' => '16',
		'message' => "Holding disk : disk space available, using as requested",
		'code' => '2800073'
	  },
          {	'source_filename' => "amcheck.c",
		'severity' => '16',
		'message' => "slot 1: contains an empty volume",
		'code' => '123'
	  },
          {	'source_filename' => "amcheck.c",
		'severity' => '16',
		'message' => "slot 2: contains an empty volume",
		'code' => '123'
	  },
          {	'source_filename' => "amcheck.c",
		'severity' => '16',
		'message' => "slot 3: contains an empty volume",
		'code' => '123'
	  },
          {	'source_filename' => "amcheck.c",
		'severity' => '16',
		'message' => ' volume \'\'',
		'code' => '123'
	  },
          {	'source_filename' => "amcheck.c",
		'severity' => '16',
		'message' => 'Taper scan algorithm did not find an acceptable volume.',
		'code' => '123'
	  },
          {	'source_filename' => "amcheck.c",
		'severity' => '16',
		'message' => '    (expecting a new volume)',
		'code' => '123'
	  },
          {	'source_filename' => "amcheck.c",
		'severity' => '16',
		'message' => 'ERROR: No acceptable volumes found',
		'code' => '123'
	  },
          {	'source_filename' => "amcheck.c",
		'severity' => '16',
		'message' => 'NOTE: host info dir',
		'code' => '2800100'
	  },
          {	'source_filename' => "amcheck.c",
		'severity' => '16',
		'message' => 'NOTE: it will be created on the next run.',
		'code' => '2800101'
	  },
          {	'source_filename' => "amcheck.c",
		'severity' => '16',
		'message' => 'NOTE: index dir',
		'code' => '2800126'
	  },
          {	'source_filename' => "amcheck.c",
		'severity' => '16',
		'message' => 'NOTE: it will be created on the next run.',
		'code' => '2800127'
	  },
          {	'source_filename' => "amcheck.c",
		'severity' => '16',
		'message' => 'Server check took 1.00 seconds',
		'code' => '2800160'
	  },
          {	'source_filename' => "amcheck.c",
		'severity' => '16',
		'message' => 'Amanda Backup Client Hosts Check',
		'code' => '2800202'
	  },
          {	'source_filename' => "amcheck.c",
		'severity' => '16',
		'message' => '--------------------------------',
		'code' => '2800203'
	  },
          {	'source_filename' => "amcheck.c",
		'severity' => '16',
		'message' => Amanda::Util::built_with_component("client")
                            ? 'ERROR: localhost: Could not access /home-incronly (/home-incronly): No such file or directory'
                            : 'ERROR: NAK localhost: execute access to \'/var/tmp/buildslave-prod/config-trunk-without_client/amanda-4.0.0alpha/prefix/libexec/amanda/noop\' denied',
		'code' => '2800211'
	  },
          {	'source_filename' => "amcheck.c",
		'severity' => '16',
		'message' => 'Client check: 1 host checked in 1.00 seconds.  1 problem found.',
		'code' => '2800204'
	  },
          {	'source_filename' => "amcheck.c",
		'severity' => '16',
		'message' => '(brought to you by Amanda x.y.z)',
		'code' => '2800016'
	  },
	  {}
        ],
      http_code => 200,
    },
    "No config") || diag("reply: " . Data::Dumper::Dumper($reply));

$rest->stop();