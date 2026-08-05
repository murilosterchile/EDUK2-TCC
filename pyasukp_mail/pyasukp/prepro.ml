(***************************************************************************)
(*                                  PYAsUKP                                *)
(*    PYAsUKP: Yet Another solver (for the) Unbounded Knapsack Problem     *)
(*           Vincent Poirriez with Nicola Yanev and Rumen Andonov          *)
(*                          LAMIH-ROI UMR CNRS 8530                        *)
(*  Copyright 1998-2005  Vincent Poirriez				   *)
(* <vincent Dot poirriez At univ-valenciennes Dot fr>                      *)
(*  This file is part of PYAsUKP.                                          *)
(*									   *)
(*  PYAsUKP is free software; you can redistribute it and/or modify        *)
(*  it under the terms of the GNU General Public License as published by   *)
(*    the Free Software Foundation; either version 2 of the License, or	   *)
(*    (at your option) any later version.				   *)
(*    PYAsUKP is distributed in the hope that it will be useful,	   *)
(*    but WITHOUT ANY WARRANTY; without even the implied warranty of	   *)
(*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the	   *)
(*    GNU General Public License for more details.			   *)
(*									   *)
(*  You should have received a copy of the GNU General Public License	   *)
(*  along with Foobar; if not, write to the Free Software		   *)
(*  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA*)
(***************************************************************************)

(* $Id: prepro.ml,v 1.2 2005/02/28 10:39:35 poirriez Exp $*)
(*prepro.ml*)
 
(** The aim of this module is to sort by increasing weights the set 
   of item types, and also to remove those which are simply dominated
 *)
open Wandp.M


 let build_remain w n =
   Array.init n (fun i -> (1+i)) 
     
let filtre_dom_simple n w p r limitc =
  let dernier = n-1 in
  let lastin = ref 0 in
  let nextout = ref 1 in
  while 
    !nextout <= dernier && Wandp.M.weight_smallereq w.(r.(!nextout)) limitc 
  do
    let nw = w.(r.(!nextout)) and np = p.(r.(!nextout)) 
    and lw = w.(r.(!lastin)) and lp = p.(r.(!lastin)) in
    if Wandp.M.equal_weights nw lw then begin
      if Wandp.M.profit_smaller lp np then r.(!lastin) <- !nextout end;
    if Wandp.M.profit_smaller lp np then begin
      incr lastin;
      r.(!lastin) <- r.(!nextout) end;
    incr nextout
  done;
  let nb = 1 + !lastin in
  let items = 
    Array.init nb (fun i -> let j = r.(i) in build_item j w.(j) p.(j)) in 
  (items, nb)
    
let smaller_interest i1 i2 = 
  ratio_smaller i1.r i2.r or 
  (equal_ratios i1.r i2.r && weight_smaller i2.w i1.w )
    
let compare_with_3_bests ((best1, best2, best3) as bests ) item =
  if smaller_interest item best2 then
    if smaller_interest item best3 then bests else (best1, best2, item)
  else if smaller_interest item best1 then (best1, item, best2)
  else (item, best1, best2)
      
let place limit i wi pi (min1, max, ((best1, best2, best3) as bests), remain) =
  if weight_smallereq wi limit then
    if weight_smaller wi min1.w then
      let newmin1 = build_item i wi pi in
      let newremain = min1::remain in
      (newmin1, max, compare_with_3_bests bests newmin1, newremain)
    else if equal_weights wi min1.w then
      if profit_smaller min1.p pi then 
        let newmin1 = build_item i wi pi in
        (newmin1, max, compare_with_3_bests bests newmin1,remain)
      else (min1, max, bests, remain)
    else if profit_smaller min1.p pi then 
      let newi = build_item i wi pi in
      let newremain = newi::remain in
      let newbests = compare_with_3_bests bests newi in
      if weight_smaller wi max.w then
        (min1, max, newbests, newremain)
      else (min1, newi, newbests, newremain)
    else  (min1, max, bests, remain)
  else  (min1, max, bests, remain)
        
let init_3 w p c =
  let n = Array.length w -1 in
  let i1,i2,i3 = ref 1, ref 1, ref 1 in
  while !i1 <= n && weight_smaller c w.(!i1)  do incr i1 done;
  if !i1 > n then
    failwith ("no item types with weight lower than the capacity "^
              (string_of_weight c))
  else begin
    i2 := !i1 + 1;
    while !i2 <= n && weight_smaller c w.(!i2)  do incr i2 done;
    if !i2 > n then begin
      i2 := !i1; i3 := !i1;
    end
    else begin
      i3 := !i2 + 1;
      while !i3 <= n && weight_smaller c w.(!i3)  do incr i3 done;
      if !i3 > n then  i3 := !i2
    end
  end;
  let item1 = build_item !i1 w.(!i1) p.(!i1) 
  and item2 = build_item !i2 w.(!i2) p.(!i2) 
  and item3 = build_item !i3 w.(!i3) p.(!i3) 
  in
  let imin1,imin2,max =
    if weight_smaller item1.w item2.w then
      if weight_smaller item2.w item3.w then (item1,item2,item3)
      else if weight_smaller item1.w item3.w then (item1,item3,item2)
      else (item3,item1,item2)
    else 
      if weight_smaller item1.w item3.w then (item2,item1,item3)
      else if weight_smaller item2.w item3.w then (item2,item3,item1)
      else (item3,item2,item1)
  in
  let ibest1,ibest2,ibest3 =
    if ratio_smaller imin1.r imin2.r then
      if ratio_smaller imin2.r max.r then (max,imin2,imin1)
      else if ratio_smaller imin1.r max.r then (imin2,max,imin1)
      else (imin2,imin1,max)
    else 
      if ratio_smaller imin1.r max.r then (max,imin1,imin2)
      else if ratio_smaller imin2.r max.r then (imin1,max,imin2)
      else (imin1,imin2,max)
  in (imin1,max,(ibest1,ibest2,ibest3),[])

let fold_left2i istart f a1 a2 x =
  let n = Array.length a1 in
  if Array.length a2 <> n then invalid_arg("array.fold_left2")
  else
  let r = ref x in
  for i = istart to n - 1 do
    r := f i (Array.unsafe_get a1 i) (Array.unsafe_get a2 i) !r
  done;
  !r
    
(** [ends_bests_others] computes no more than 6 comparisons for each item type, thus its
   complexity is in $\Theta(6n)+O(nlog(n))$ the nlog(n) part is for sorting the remaining
   items.*)
let ends_bests_others w p limit = 
  let  accu = (init_3 w p limit) in
  fold_left2i 1 (place limit) w p accu
    
(* [ends_bests_others w p limit] returns [min1, max, bests, remains] where
   [min1] is the minimal weighted item type;
   [max] is the heavyest item type;
   [bests] is a 3-uple of the 3 best item types in term of ratio profit/weight
   [remains] is an un-ordered list of the item types with weight smaller 
   than [limit]
 *)
    
let fold_left_from deb f x a =
  let r = ref x in
  for i = deb to Array.length a - 1 do
    r := f !r (Array.unsafe_get a i)
  done;
  !r
    
let remove_in_array_from start last pred a  =
  let nextin = ref start in
  for i = start to last do
    if not(pred a.(i) ) then begin
      a.(!nextin) <- a.(i);
      incr nextin
    end
  done;
  !nextin - start

let remove_in_array pred a n =
  remove_in_array_from 0 (n-1) pred a 
    
let fold_left_to f (a,iend) x =
  let r = ref x in
  for i = 0 to iend-1 do
    r := f (Array.unsafe_get a i) !r
  done;
  !r
    
    

