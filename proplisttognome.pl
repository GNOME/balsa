#!/usr/bin/perl -w


#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option) 
# any later version.
#    
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of 
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the  
# GNU General Public License for more details.
#  
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
# 02111-1307, USA.
#
# (c) 1997-2000 Stuart Parmenter and others, see AUTHORS for a list of people
#

#
# $Id$
#

undef $/;

chdir $ENV{'HOME'} if (defined($ENV{'HOME'}));

open IN, ".balsarc";
open OUT, ">.gnome_private/balsa";
select OUT;

$_ = <IN>;
s/\n/ /g;
my @top = split /(\{|\})/;

shift(@top);

my $mbox;
while($_ = shift(@top)) {
  if(/Accounts/) {
    shift(@top);
    $mbox = shift(@top);
    do {
      $mbox =~ s/\W//g;
      print "[mailbox-$mbox]\n";
      shift(@top);
      my @props = split(/;/, shift(@top));
      while($_ = shift(@props)) {
	s/local/LibBalsaMailboxLocal/ if(/Type\s+=\s+local/);
	s/IMAP/LibBalsaMailboxImap/ if(/Type\s+=\s+IMAP/);
	s/POP3/LibBalsaMailboxPop3/ if(/Type\s+=\s+POP3/);
	s/\s//g;
	s/Type/type/g;
	s/Name/name/g;
	s/Path/path/g;
	s/Server/server/g;
	s/Port/port/g;
	s/Username/username/g;
	s/Password/password/g;
	print "$_\n";
      }
      print "\n";	  
      shift(@top);
      $mbox = shift(@top);
    } until( $mbox =~ /;\s+$/);
  } elsif (/Globals/) {
#    print "\n[Globals]\n";
#    shift(@top);
#    my @props = split(/;\s/, shift(@top));
#    while($_ = shift(@props)) {
#      print "$_\n";
#    }
  }
}

exit;
