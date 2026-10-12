use strict; use warnings;
my ($moddir)=@ARGV;
my %clr;
for my $f (glob("$moddir/decisions/*.txt"),glob("$moddir/events/*.txt"),glob("$moddir/common/*.txt"),glob("$moddir/technologies/*.txt"),glob("$moddir/inventions/*.txt")){
  open my $h,'<',$f or next; while(<$h>){ s/#.*//; $clr{$1}++ while /clr_country_flag\s*=\s*(\S+)/g; } close $h; }
my ($total,$tag,$once,$onceTag,$onceNested,$oncecleared)=(0,0,0,0,0,0); my @list;
for my $f (glob("$moddir/decisions/*.txt")){
  open my $h,'<',$f or next; my $t=join '',map{s/#.*//;$_}<$h>; close $h;
  my @tok=$t=~/(\{|\}|[^\s{}]+)/g;
  my $d=0; my @stack; my %cur; my $notd=0;
  for(my $i=0;$i<@tok;$i++){ my $k=$tok[$i];
    if($k eq '{'){ $d++; push @stack,$tok[$i-2]//''; if($d==2){ %cur=(name=>$tok[$i-2],gate=>{},setf=>{},settop=>{},ptop=>[]); }
                   if($d>=4 && $stack[2] eq 'potential' && $stack[-1] eq 'NOT'){ $notd=$d unless $notd; } }
    elsif($k eq '}'){ if($notd && $d==$notd){ $notd=0; }
      if($d==2 && $cur{name}){ $total++; my %c=%cur;
        my $taglock = (grep{/^tag=[A-Z0-9]{3}$/} @{$c{ptop}}) ? 1:0; $tag+=$taglock;
        my ($hit)=grep{ $c{setf}{$_} && !$clr{$_} } keys %{$c{gate}};
        my ($hitc)=grep{ $c{setf}{$_} && $clr{$_} } keys %{$c{gate}};
        $oncecleared++ if !$hit && $hitc;
        if($hit){ $once++; $onceTag++ if $taglock; $onceNested++ unless $c{settop}{$hit}; push @list,[$c{name},$hit,$taglock,$c{settop}{$hit}?1:0]; }
      } $d--; pop @stack; }
    elsif($d>=3 && $cur{name} && ($tok[$i+1]//'') eq '='){ my $v=$tok[$i+2]//''; my $sec=$stack[2];
        if($sec eq 'potential'){ push @{$cur{ptop}},"$k=$v" if $d==3; $cur{gate}{$v}=1 if $k eq 'has_country_flag' && $notd; }
        if($sec eq 'effect' && $k eq 'set_country_flag'){ $cur{setf}{$v}=1; $cur{settop}{$v}=1 if $d==3; } }
  }
}
print "decisions=$total tag_locked=$tag\n";
print "once-pattern, flag never cleared anywhere: $once (tag_locked=$onceTag; set only nested in effect=$onceNested)\n";
print "once-pattern but flag IS cleared somewhere: $oncecleared\n";
open my $o,'>',"$moddir-once-decisions.txt"; print $o join("\n",map{"$_->[0]\tflag=$_->[1]\ttag_locked=$_->[2]\tset_top=$_->[3]"}@list),"\n"; close $o;
