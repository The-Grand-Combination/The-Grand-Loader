#!/usr/bin/perl
# Builds TGC's common/se_partitions.txt from the mod's own data:
#   A. history/provinces: every core a province carries (at start or by a dated entry);
#   B. events/ and decisions/: `add_core = TAG` / `secede_province = TAG` in a region or province scope,
#      or in a block whose limit names regions / provinces.
# usage: perl partitions.pl <TGC dir> <out file> <report file>
use strict;
use warnings;

my ($root, $out, $report) = @ARGV;
my %skip_tags = map { $_ => 1 } qw(GER ITA CHI HND REB
    ADV ARA ARG ATC BYZ CFB D01 DNB EIC FAS FIL GCF GCO HNS IBR ISR KMT KPB KUK LPL LTU NGF PAC PBC PLC
    SCA TKS UIL USS UTI YUG
    ENL TKG VFR
    SGF DUR ARU YEM CRB INO MLY);  # unions over other nations' homelands; formations of several countries;
    # a great power's own alternate identity (ENL: `ENG = { all_core = { remove_core = ENG } } change_tag =
    # ENL` in decisions/ENG.txt, what the UK itself becomes losing Scotland and Ireland; TKG: "Shogunate
    # Japan", the Great War's Japan dismantling treats `tag = JAP` and `tag = TKG` as the same country; VFR:
    # "French Kingdom" / "Vichy France" depending on government, a puppet decisions/Germany.txt makes of
    # France itself, `change_tag = FRA` reverts it) -- giving a third party this tag would hand land to a
    # disguised FRA/ENG/JAP, not to a real other country. Same alternate-identity shape found this pass:
    # SGF (Austria/Germany's "South German Federation", decisions/Germany.txt: `KUK = { all_core = {
    # remove_core = KUK add_core = AUS } } change_tag = SGF`, a sibling of the already-skipped GCF/DNB/KUK);
    # DUR ("Durrani Empire", decisions/Afghanistan.txt: `all_core = { add_core = DUR remove_core = THIS }
    # change_tag = DUR` on AFG itself); ARU ("Arabian Union", decisions/Arabia.txt: the same `remove_core =
    # THIS add_core = ARU` / `change_tag = ARU` shape, a sibling of the already-skipped ARA). Formations of
    # several countries found this pass: YEM (decisions/Arabia.txt: LHJ, FDL, KTH and MHR each hand their
    # cores to YEM then `change_tag = YEM` -- keep the four emirates, not the union); CRB ("Caribbean
    # Federation", decisions/Caribbean.txt: annexes other Caribbean countries via `annex_to = THIS` then
    # `change_tag = CRB`, a sibling of the already-skipped ATC/GCO federations -- unlike UCA, which keeps its
    # own UCA_* cores and is a real country, not a union); INO ("Indonesia", decisions/Indonesia.txt) and MLY
    # ("Malaysia", decisions/ENG.txt): both are built entirely out of other tags' cores (BRU, NET, ATJ, ENG)
    # with none of their own, the same "culture union over a colonial holding" shape as the already-skipped
    # HND -- keep the individual sultanates/colonies (ATJ, BAL, BRU, JOH, ...) instead
my %not_tags = map { $_ => 1 } qw(THIS FROM ROOT);

sub slurp { my ($p) = @_; open(my $f, '<:raw', $p) or return ''; local $/; my $t = <$f>; close $f; return $t; }

sub words {
    my ($t) = @_;
    $t =~ s/#[^\n]*//g;
    my @w;
    while ($t =~ /("[^"]*"|[{}=]|[^\s{}=]+)/g) { push @w, $1; }
    return \@w;
}

# [key, value] pairs; a value is a string or an array ref of pairs
sub parse {
    my ($w, $i) = @_;
    my @items;
    while ($$i < @$w) {
        my $x = $w->[$$i++];
        return \@items if $x eq '}';
        if ($x eq '{') { push @items, ['', parse($w, $i)]; next; }
        if ($$i < @$w && $w->[$$i] eq '=') {
            $$i++;
            my $v = $w->[$$i++] // '';
            if ($v eq '{') { push @items, [$x, parse($w, $i)]; }
            else { push @items, [$x, $v]; }
        } else {
            push @items, ['', $x];  # a bare word (a list member)
        }
    }
    return \@items;
}

# map/region.txt
my (%region, @region_order);
{
    my $w = words(slurp("$root/map/region.txt"));
    my $i = 0;
    for my $it (@{ parse($w, \$i) }) {
        next unless ref $it->[1];
        my @ids = map { $_->[1] } grep { !ref $_->[1] && $_->[1] =~ /^\d+$/ } @{ $it->[1] };
        next unless @ids;
        $region{$it->[0]} = [@ids];
        push @region_order, $it->[0];
    }
}
my %province_region;
for my $r (@region_order) {
    next if $r =~ /_(REGION|COAST|SEA|PLANTATIONS)$/ || $r !~ /^[A-Z][A-Z0-9]{2}_\d+$/;
    $province_region{$_} //= $r for @{ $region{$r} };
}

my (%cores, %src);  # tag -> {province -> 1}; tag -> {source -> 1}
sub grant { my ($tag, $ids, $source) = @_; return if $not_tags{$tag} || $skip_tags{$tag}; $cores{$tag}{$_} = 1 for @$ids; $src{$tag}{$source} = 1 if @$ids; }

# A. province history
my @history;
{
    my @dirs = ("$root/history/provinces");
    while (my $d = shift @dirs) {
        opendir(my $h, $d) or next;
        for my $e (readdir $h) {
            next if $e =~ /^\./;
            my $p = "$d/$e";
            if (-d $p) { push @dirs, $p; next; }
            push @history, $p if $e =~ /^(\d+)\s*-.*\.txt$/i;
        }
        closedir $h;
    }
}
for my $p (@history) {
    my ($id) = $p =~ m{/(\d+)\s*-[^/]*$};
    my $t = slurp($p);
    $t =~ s/#[^\n]*//g;
    while ($t =~ /\badd_core\s*=\s*([A-Z][A-Z0-9]{2})\b/g) { grant($1, [$id], 'history'); }
}

# B. scripts
sub places_in_limit {
    my ($items, $out) = @_;
    for my $it (@$items) {
        my ($k, $v) = @$it;
        if (ref $v) {
            places_in_limit($v, $out) if $k eq 'OR' || $k eq 'AND' || $k eq '';
            next;
        }
        if ($k eq 'region' && $region{$v}) { push @$out, @{ $region{$v} }; }
        elsif ($k eq 'province_id' && $v =~ /^\d+$/) { push @$out, $v; }
    }
}
sub walk {
    my ($items, $file) = @_;
    for my $it (@$items) {
        my ($k, $v) = @$it;
        next unless ref $v;
        my $tag_of = sub { my ($key) = @_; map { $_->[1] } grep { !ref $_->[1] && $_->[0] eq $key && $_->[1] =~ /^[A-Z][A-Z0-9]{2}$/ } @$v };
        my @cores = $tag_of->('add_core');
        my @secedes = $tag_of->('secede_province');
        # A region or province scope: its cores only (`FRA_412 = { secede_province = BAV }` hands land to
        # a winner). A block limited to regions: secessions too (BRZ's 96058 splits Brazil that way).
        if (@cores && (($region{$k} && $k !~ /_(COAST|SEA|PLANTATIONS)$/) || $k =~ /^\d+$/)) {
            grant($_, $region{$k} ? $region{$k} : [$k], $file) for @cores;
        } elsif (@cores || @secedes) {
            my ($limit) = grep { $_->[0] eq 'limit' && ref $_->[1] } @$v;
            my @ids;
            places_in_limit($limit->[1], \@ids) if $limit;
            grant($_, \@ids, $file) for @cores, @secedes;
        }
        walk($v, $file);
    }
}
for my $dir ('events', 'decisions') {
    opendir(my $h, "$root/$dir") or next;
    for my $e (sort readdir $h) {
        next unless $e =~ /\.txt$/i;
        my $w = words(slurp("$root/$dir/$e"));
        my $i = 0;
        walk(parse($w, \$i), "$dir/$e");
    }
    closedir $h;
}

# names
my %name;
{
    opendir(my $h, "$root/localisation") or die;
    for my $e (sort readdir $h) {
        next unless $e =~ /\.csv$/i;
        for my $line (split /\r?\n/, slurp("$root/localisation/$e")) {
            $name{$1} //= $2 if $line =~ /^([A-Z][A-Z0-9]{2});([^;]+);/;
        }
    }
}
my %is_country;
{
    my $t = slurp("$root/common/countries.txt");
    $t =~ s/#[^\n]*//g;
    $is_country{$1} = 1 while $t =~ /^\s*([A-Z][A-Z0-9]{2})\s*=/mg;
}

# output: whole regions by name, the rest as provinces
open(my $o, '>:raw', $out) or die;
open(my $r, '>:raw', $report) or die;
print $o join("\r\n", split /\n/, <<'HEAD') . "\r\n";
# Partitions (Script Extender): the countries a loser can be broken into by treaty_partition in a
# dismantle, whoever holds the land then. TAG = { REGION ... provinces = { id ... } }; regions of
# map/region.txt. A term takes one of the loser's states holding some of TAG's land (the whole state
# goes) and makes the listed provinces TAG's cores; TAG is released when it does not exist.
#
# Generated by script_extender/tools/partitions.pl from the mod's own data, so regenerate rather than edit:
# - every core in history/provinces, at start or by a dated entry;
# - every `add_core` an event or decision gives a tag in a region or province scope, or in a block limited
#   to regions / provinces (the Great War dismantling events, releases, independence decisions);
# - every `secede_province` in such a limited block (Brazil's own dismantling, 96058).
# Left out: REB, the culture unions over other nations' homelands (GER, ITA, CHI, HND) and the formations
# that merge several countries (KUK, PLC, GCO, USS, ISR, ...), and the great powers under another tag
# (VFR, ENL, TKG, GCF, UIL: France, the UK, Japan, Germany, the USA).
HEAD
my $count = 0;
for my $tag (sort keys %cores) {
    unless ($is_country{$tag}) { print $r "not a country: $tag\n"; next; }
    my %left = %{ $cores{$tag} };
    my @regions;
    for my $rg (@region_order) {
        next if $rg !~ /^[A-Z][A-Z0-9]{2}_\d+$/;
        my @ids = @{ $region{$rg} };
        next unless @ids && !grep { !$left{$_} } @ids;
        push @regions, $rg;
        delete $left{$_} for @ids;
    }
    my @provinces = sort { $a <=> $b } keys %left;
    my $body = join(' ', @regions);
    $body .= ($body ? ' ' : '') . 'provinces = { ' . join(' ', @provinces) . ' }' if @provinces;
    my $label = $name{$tag} // '';
    print $o "$tag = { $body }" . ($label ? "  # $label" : '') . "\r\n";
    print $r "$tag ($label): " . scalar(keys %{ $cores{$tag} }) . " provinces from " . join(', ', sort keys %{ $src{$tag} }) . "\n";
    ++$count;
}
close $o;
print $r "total $count\n";
close $r;
