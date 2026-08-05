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



(* $Id: select.ml,v 1.2 2005/02/28 10:39:35 poirriez Exp $ *)
(*select.ml*)

open Wandp.M


(** Sélectionne le plus petit élément d'un tableau *)
let the_min comp tab fin =
  let r = ref tab.(0) in
  for i = 1 to fin do if comp tab.(i) !r then r := tab.(i) done;
  !r

let is_lighter i j = Wandp.M.weight_smallereq  i.w j.w
let lightest tab fin = the_min is_lighter tab fin



(** We have to return a weight ordered set of the item types lighter than a given limit and
   an un-ordered set of those heavier than this limit (at least, it must be easy to obtain
   the lightest of those heavyer item types. We return these sets together
   with the two minimal weighted item types, the maximal weighted one,
   and the three best in term of ratio profit/weight. 
   This traversal of the item types will be also used to try to detect
   some multiple dominated item types (not all), these will not be stored
   in the returned ordered set.
   We assume that there is more than one item type.
*)

(** Some code to select the n lightest item types using a quicksort
   partition scheme. *)
;;
 let permute_element tab n p = 
   let aux = tab.(n) in 
   tab.(n) <- tab.(p) ; 
   tab.(p) <- aux  
       
let choisir_pivot tab debut fin = 
  debut + Random.int (fin - debut)
    
let permute_pivot comp tab debut fin ind_pivot = 
  permute_element tab debut ind_pivot ;
  let i = ref (debut+1) 
  and j = ref fin 
  and pivot = tab.(debut) in
  while !i < !j do 
    if comp tab.(!j)  pivot then decr j 
    else
      begin
        permute_element tab !i !j ;
        incr i
      end
  done ;
  if not(comp pivot tab.(!i) ) then decr i ;
  permute_element tab debut !i ;
  !i 
    
let rec quick comp tab debut fin = 
  if debut < fin then 
    let pivot = choisir_pivot tab debut fin in 
    let place_pivot = permute_pivot comp tab debut fin pivot in 
    quick comp (quick comp tab debut (place_pivot - 1)) (place_pivot + 1) fin
  else tab 

(** Permuter les éléments entre i et i+k avec ceux entre n-k et n 
   on garanti que i <= n-k. On ne bouge pas les élements d'indice j tel
   que j>n-k et j < i+k
*)
let switchblock tab ideb ifin =
  let ilast = Array.length tab  -1 in
  let k = ifin - ideb in
  let j = ilast - ifin -1 in
  for i = 0 to min k j do
    permute_element tab (ideb+i) (ilast - i)
  done;
  k+1

(** [split_n_best comp tab debut fin n] positionne aux [n] premières 
   positions de [tab] les éléments tels que, pour tout j < n, k>=n,
   comp tab.(j) tab.(k). Les n premiers éléments de tab ne sont pas
   ordonnés entre eux.*)
let rec split_n_best comp tab debut fin n =
  if fin - debut < n then () else
  if debut < fin then
    let pivot = choisir_pivot tab debut fin in
    let place_pivot = permute_pivot comp tab debut fin pivot in
    if place_pivot = n-1 || place_pivot = n then () 
    else if place_pivot < n then 
      split_n_best comp tab (place_pivot+1) fin (n - place_pivot -1)
    else split_n_best comp tab debut (place_pivot -1)  n;;

(** [partition_in_three elim comp tab debut fin ind_pivot] effectue
   les permutations nécessaires sur [tab] pour que les éléments d'indice
   compris entre [debut] et [fin] inclus vérifient les propriétés: les premiers
   éléments vérifient [comp tab.(i) tab.(ind_pivot) = true], les suivants
   vérifient [comp tab.(i) tab.(ind_pivot)  = false] et les derniers vérifient
   [elim tab.(ind_pivot) tab.(i) = true] le couple retourné est constitué de 
   l'indice du pivot et de l'indice du premier élément de la troisième partie.
 *)
let partition_in_three elim  comp (tab: item array) debut fin ind_pivot = 
  permute_element tab debut ind_pivot ;
  let i = ref (debut+1) 
  and j = ref fin 
  and k = ref (fin+1)
  and pivot = tab.(debut) in
  while !i < !j do 
    if comp tab.(!j)  pivot then
      begin
        permute_element tab !i !j ;
        incr i
      end
    else if elim pivot tab.(!j) then begin
      decr k;
      permute_element tab !k !j;
      decr j
    end
    else  decr j 
  done ;
  if comp pivot tab.(!i) then decr i ;
  permute_element tab debut !i ;
  (!i, !k) 
    
(** [multi_part comp elim nb tab] renvoie une liste de couples d'indices {% $[(d_0,f_0);\ldots
   (d_s,f_s)]$%} tels que:
   pour tout {% $i<j$ $k\in [d_i..f_i]$%} et {% $m\in [d_j..f_j]$%}
   implique [comp tab.(k) tab.(m)]. 
   {% $f_0 - d_0 \leq nb$%}
   Les éléments d'indices {% $r$%} dans 
   {% $ f_j<r<d_{j+1}$%} sont des éléments éliminés.
 *)
let multi_part comp elim nb deb fin tab =
  let rec part_aux deb fin remain =
    let long = fin - deb +1 in
    if long <= nb then (deb,fin)::remain else begin
      let ind_pivot = deb + Random.int (long-1) in
      let (i,fin') = 
        partition_in_three elim  comp tab deb fin ind_pivot in
      let new_remain = if i+1=fin' then remain else (i+1,fin'-1)::remain
      in
      part_aux deb i new_remain
    end
  in part_aux deb fin [];;

(** Sélectionner les [nb] plus légers en

- Supprimant parmi eux ceux qui sont simplement dominés
- Renvoyant ceux qui restent

*)

let is_heavyer i j = Wandp.M.weight_smallereq  j.w i.w
let has_better_p i j = Wandp.M.weight_smallereq j.p i.p 

let rec next_lightest tab remains nb2select pacc =
  match remains with 
  | [] -> None
  | (deb,fin)::suite when fin - deb >= nb2select ->
      let split_head =
        multi_part is_lighter has_better_p nb2select deb fin tab
      in
      next_lightest tab (split_head @ suite) nb2select pacc
  | (deb,fin)::suite ->
      let rp = ref pacc in
      let nbreste =
        Prepro.remove_in_array_from deb fin  
          (fun item -> 
            let b = Wandp.M.profit_smaller !rp item.p in
            if b then rp := item.p;
            not b)
          (quick is_heavyer tab deb fin)
      in
      if nbreste = 0 then 
        next_lightest tab suite nb2select pacc 
      else
       Some ((Array.sub tab deb nbreste,nbreste), suite)

(** Supprimer en O(n*n) les éléments j vérifiant il existe i < j telque
    elimin tab.(i) tab.(j)
*)

let elim2pass_from elim tab st nb =
  let garde = Array.create nb true in
  for i = st to st+  nb -2 do
    for j = i+1 to nb-1 do
      if garde.(j) && elim tab.(i) tab.(j) then garde.(j) <- false
    done
  done;
  let nextin = ref st in
  for i = st to st + nb -1 do
    if garde.(i) then begin
      tab.(!nextin) <- tab.(i);
      incr nextin
    end
  done;
  !nextin 
let elim2pass elim tab nb = elim2pass_from elim tab 0 nb 
