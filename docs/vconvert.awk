# Usage:
# First export your netscape address book to an LDIF file.
# Then run: awk -f vconvert.awk filename.ldif > filename.gcrd
# This script will "prefer" work numbers and addresses by default;
# it will list a work phone (if found) as the preferred phone and will
# assume netscape's single address to be a work address.
# To reverse this behavior, add "-v prefer=home" to the awk command line.
#
# Dependencies:  awk, obviously.  mmencode is used to decode multi-line
# fields, which are base64 encoded in NAB. If mmencode isn't on your system 
# install the 'metamail' package.
#
# Written by Stewart Evans <stewart@lunula.com>, freely redistributable. 


BEGIN { FS=": "  
       if ( length(prefer) == 0 ) { prefer="work" } } 

NF == 0 { endcard(); next }
$1 ~ /:$/ { decode($1, $2) }
$1 == "dn"  {print "BEGIN:VCARD"; incard=1 }
$1 == "sn" { surname=$2  }
$1 == "givenname" { givenname=$2 }
$1 == "xmozillanickname" { print "FN:" $2 }
$1 == "mail" { print "EMAIL;INTERNET:" $2 }
$1 == "o"    { print "ORG:" $2 }
$1 == "telephonenumber"    { print "TEL;WORK:" $2 
                       if ( prefer == "work" ) print "TEL;PREF:" $2 }
$1 == "homephone"    { print "TEL;HOME:" $2 
                       if ( prefer == "home" ) print "TEL;PREF:" $2 }
$1 == "cellphone"    { print "TEL;VOICE:" $2 }
$1 == "facsimiletelephonenumber"    { print "TEL;FAX:" $2 }
$1 == "pagerphone"    { print "TEL;MSG:" $2 }
$1 == "title"    { print "TITLE:" $2 }
$1 == "locality"    { city=$2 }
$1 == "st"    { state=$2 }
$1 == "postalcode"    { zip=$2 }
$1 == "countryname"    { country=$2 }
$1 == "streetaddress"    { street1=$2 }

END { endcard() }

function endcard() { if ( incard == 0 ) { next }
                     name=surname ";" givenname
                     if ( length(name) > 1 )  
		         { printf "N:%s\n", name }
		     adr=";" street1 ";" street2 ";" city ";" state ";" zip ";" country
		     if ( prefer == "home" ) {adrtype="HOME"}
		     else { adrtype="WORK" }
		     if ( length( adr ) > 6)
                        {printf "ADR;%s:%s\n", adrtype, adr }
            printf "END:VCARD\n\n" 
	    surname=""
	    givenname=""
	    street1=""
	    street2=""
	    city=""
	    state=""
	    zip=""
	    country=""
	    incard=0
}

function decode(tag, value) {
     while ( getline && (NF < 2) ) {  value = value substr($1,2) } 
     getline pid <"/dev/pid"
     fname="/tmp/awk" pid
     echo pid fname
     system("echo " value "| /usr/bin/mmencode -u > " fname) 
     if ( $1 == "description:" )  {
         while ( getline dline < fname ) 
             { if (length(deval) > 0 ) { deval=deval "=0A=\n" dline }
	        else { deval=dline } }
         printf "NOTE;QUOTED-PRINTABLE:%s\n", deval }
     else if ( $1 == "streetaddress:" ) {
         getline street1 < fname
	 getline street2 < fname
     }
}
