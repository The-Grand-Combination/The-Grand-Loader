use strict; use warnings;
my ($m)=@ARGV;
my (%clr,%setc,%setg,%clrg,%tags);
my @all=(glob("$m/decisions/*.txt"),glob("$m/events/*.txt"),glob("$m/common/*.txt"),glob("$m/technologies/*.txt"),glob("$m/inventions/*.txt"),glob("$m/history/countries/*.txt"),glob("$m/history/diplomacy/*.txt"));
for my $f (@all){ open my $h,'<',$f or next; while(<$h>){ s/#.*//;
  $clr{$1}++ while /clr_country_flag\s*=\s*(\S+)/g; $setc{$1}++ while /set_country_flag\s*=\s*(\S+)/g;
  $setg{$1}++ while /set_global_flag\s*=\s*(\S+)/g; $clrg{$1}++ while /clr_global_flag\s*=\s*(\S+)/g; } close $h; }
open my $h,'<',"$m/common/countries.txt"; while(<$h>){ $tags{$1}=1 if /^\s*([A-Z0-9]{3})\s*=/ } close $h;
my %first; my %cat; my $total=0; my %catnames;
my $tagn=0;
for my $f (glob("$m/decisions/*.txt")){
  open my $h,'<',$f or next; my $t=join '',map{s/#.*//;$_}<$h>; close $h;
  my @tok=$t=~/(\{|\}|[^\s{}]+)/g;
  my $d=0; my @stack; my %c; my $notd=0; my $firstset=0;
  for(my $i=0;$i<@tok;$i++){ my $k=$tok[$i];
    if($k eq '{'){ $d++; push @stack,$tok[$i-2]//''; if($d==2){ %c=(name=>$tok[$i-2],cats=>{},setf=>{},gate=>{}); $firstset=0; }
      if($d>=4 && $stack[2] eq 'potential' && $stack[-1] eq 'NOT'){ $notd=$d unless $notd; }
      if($d==4 && $stack[2] eq 'potential' && !$firstset){ $first{$stack[-1].'{}'}++; $firstset=1; } }
    elsif($k eq '}'){ $notd=0 if $notd && $d==$notd;
      if($d==2 && $c{name}){ $total++;
        for my $g (keys %{$c{gate}}){ $c{cats}{once_country_flag}=1 if $c{setf}{$g} && !$clr{$g}; }
        for my $k2 (keys %{$c{cats}}){ $cat{$k2}++ }
        $cat{any_dead_pattern}++ if keys %{$c{cats}};
      } $d--; pop @stack; }
    elsif($d>=3 && $c{name} && ($tok[$i+1]//'') eq '='){ my $v=$tok[$i+2]//''; my $sec=$stack[2];
      if($sec eq 'effect' && $k eq 'set_country_flag'){ $c{setf}{$v}=1 }
      next unless $sec eq 'potential';
      if($d==3 && !$firstset){ $first{$k}++; $firstset=1; }
      my $top = ($d==3) || ($notd && $d==$notd && $notd==4);   # top-level or inside a top-level NOT
      next unless $top;
      if($notd){
        $c{gate}{$v}=1 if $k eq 'has_country_flag';
        $c{cats}{after_year}=1 if $k eq 'year';
        $c{cats}{once_global_flag}=1 if $k eq 'has_global_flag' && $setg{$v} && !$clrg{$v};
        $c{cats}{not_invention_or_tech}=1 if $k eq 'invention' || ($k !~ /^(has_|tag|exists|year|month|civilized|is_|war|owns|primary_culture|government|ruling_party|nationalvalue|religion|capital|ai|controls|slavery|in_sphere|part_of_sphere|vassal_of|substate_of|has_country_modifier|literacy|prestige|plurality|money|badboy|war_exhaustion|has_country_flag|has_global_flag|poor_strata_militancy|rich_strata_militancy|middle_strata_militancy|average_militancy|average_consciousness|total_amount_of_divisions|total_amount_of_ships|num_of_ports|num_of_cities|num_of_vassals_no_substates|num_of_substates|num_of_allies|num_of_revolts|revolt_percentage|rank|constructing_cb|has_recently_lost_war|recruited_percentage|unemployment|big_producer|region|continent|culture|accepted_culture|is_culture_group|culture_group|has_pop_culture|has_pop_religion|has_pop_type|is_colonial|is_core|is_capital|is_coastal|is_overseas|life_rating|has_building|has_factories|has_leader|empty|produces|state_id|trade_goods|has_national_minority|terrain|minorities|units_in_province|controlled_by|owned_by|country_units_in_state|military_score|industrial_score|total_pops|total_num_of_ports|pop_majority_culture|pop_majority_religion|pop_majority_ideology|pop_majority_issue|pop_type|can_build_factory|can_build_fort|can_build_railway|can_create_vassals|can_nationalize|can_release|crisis_|culture_has_union_tag|has_cultural_sphere|has_empty_adjacent_state|has_unclaimed_cores|involved_in_crisis|is_claim_crisis|is_cultural_union|is_disarmed|is_greater_power|is_secondary_power|is_sphere_leader_of|is_possible_vassal|is_releasable_vassal|is_state_capital|is_state_religion|is_vassal|is_substate|is_our_vassal|is_liberation_crisis|is_independant|is_mobilised|is_next_reform|is_primary_culture|is_accepted_culture|is_ideology_enabled|is_canal_enabled|is_blockaded|is_slave|lost_national|mobilisation_size|national_provinces_occupied|nationalism|neighbour|alliance_with|truce_with|casus_belli|military_access|owned_by|controlled_by|relation|stronger_army_than|consciousness|militancy|election|political_reform_want|social_reform_want|social_movement_strength|political_movement_strength|social_spending|military_spending|education_spending|administration_spending|poor_tax|middle_tax|rich_tax|tariffs|upper_house|unit_has_leader|unit_in_battle|unit_in_siege|this_culture_union|number_of_states|total_sunk_by_us|total_sea_battles|total_land_battles|low_tax_tags|high_tax_tags|rebel_power_fraction|revanchism|invention|part_of_sphere|has_crime|has_province_flag|has_province_modifier|is_colonial|has_national_focus|has_rebel_type|is_overseas|workers|has_country_modifier|exists|tag|year|month|war|owns|has_leader)$/ && $k =~ /^[a-z_]+$/ && $v eq 'yes' && 0);
        $c{cats}{not_civilized}=1 if $k eq 'civilized' && $v eq 'yes';
      } else {
        $c{cats}{unreachable_country_flag}=1 if $k eq 'has_country_flag' && !$setc{$v};
        $c{cats}{unreachable_global_flag}=1 if $k eq 'has_global_flag' && !$setg{$v};
        $c{cats}{unknown_tag}=1 if $k eq 'tag' && $v=~/^[A-Z0-9]{3}$/ && !$tags{$v};
        $c{cats}{not_civilized}=1 if $k eq 'civilized' && $v eq 'no';
        $c{cats}{tag_locked}=1 if $k eq 'tag' && $v=~/^[A-Z0-9]{3}$/;
        $c{cats}{invention_or_tech_positive}=1 if $k eq 'invention';
      }
    }
  }
}
print "decisions=$total\n"; print "$_\t$cat{$_}\n" for sort{$cat{$b}<=>$cat{$a}} keys %cat;
print "\nfirst condition of potential:\n"; my $n=0; for (sort{$first{$b}<=>$first{$a}} keys %first){ print "$_\t$first{$_}\n"; last if ++$n>=25; }
