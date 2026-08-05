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



(* $Id: datagen.ml,v 1.6 2005/04/27 13:09:32 poirriez Exp $ *)
(*datagen.ml*)

open Wandp.M
(* This stuff is to avoid to try to allocate too large arrays*)
let deux31 = truncate (2. ** 30. -. 1.)
let maxint = let temp = deux31 in
 if temp + 1 <0 then temp else truncate (2. ** 61. -. 1.)
let maxarray = if maxint > deux31 then 2000000 else 1000000;;
 let max_profit x y = if Wandp.M.profit_smaller x y then y else x

(*Just to allow to generate 0 *)
let randomint x = try Random.int x with _ -> x
   
(* This one is to shuffle randomly pairwise two arrays.*)
let shuffle tab1 tab2 = 
  let m = Array.length tab1 -1 in
  for i = 1 to m do
    let nj = 1+randomint m in
    let wj = tab1.(nj) and pj = tab2.(nj) in
    tab1.(nj)<- tab1.(i); tab2.(nj)<- tab2.(i); tab1.(i)<- wj; tab2.(i)<-pj
  done

let random_warray m wmax wmin =
  let r= Array.create (m+1) zerow
  in
  let wsup = substract_weight wmax wmin 
  in
  for i = 1 to m do
    r.(i) <- add_weights wmin (rand_weight wsup);
  done;
  r
  
let rec trouve_trou tab debut fin =
  if debut = fin then (fin + 1) else
  let suiv = debut+1 in
  if tab.(debut) + 1 = tab.(suiv) then trouve_trou tab (suiv) fin
  else suiv
      
let decale tab top bot=
  for i = top downto bot do tab.(i+1) <- tab.(i) done
    
let random_incr m wmax wmin start  =
  let m' = ref 1 in
  let wmat = Array.create (m+1) zerow in
  let bsup = substract_weight wmax wmin in
  wmat.(1) <-  start ;
  let notfound = ref true 
  and bmin = ref 1 and bmax = ref 1 and med = ref 1
  in
  while !m' < m  do
    let tmp = add_weights wmin (rand_weight bsup) in
    notfound :=true;
    bmin := 1;
    bmax:= !m' +1;
    med := ((!bmin + !bmax)/2);
    while !notfound do
      if  !bmax- !bmin =1  then
        (notfound := false;
         if weight_smaller wmat.(!bmin) tmp then 
           (decale wmat !m' !bmax;
            wmat.(!bmax) <- tmp;
            incr m')
         else if weight_smaller tmp  wmat.(!bmin) then
           (for i = !m' downto !bmin do wmat.(i+1) <- wmat.(i) done;
            wmat.(!bmin) <- tmp;
            incr m')
        )
      else if equal_weights tmp  wmat.(!med) then begin
        notfound :=false;
        let i = trouve_trou wmat !med !m' in
        let suiv = if i<= !m' then wmat.(i) else wmax in
        if i <= !m' then decale wmat !m' i;
        let prec = wmat.(i-1) in
        wmat.(i) <- prec +  randomint (suiv - prec);
      end
      else if weight_smaller tmp  wmat.(!med) then
        (bmax:= !med; med:= ((!bmax + !bmin)/2))
      else (bmin := !med; med:= ((!bmax + !bmin)/2))
    done
  done;
  wmat

let random_incr_prof m pmax pmin start  =
  let m' = ref 1 in
  let pmat = Array.create (m+1) zerow in
  let bsup = substract_profit pmax pmin in
  pmat.(1) <-  start ;
  let notfound = ref true 
  and bmin = ref 1 and bmax = ref 1 and med = ref 1
  in
  while !m' < m  do
    let tmp = add_profits pmin (rand_profit bsup) in
    notfound :=true;
    bmin := 1;
    bmax:= !m' +1;
    med := ((!bmin + !bmax)/2);
    while !notfound do
      if  !bmax- !bmin =1  then
        (notfound := false;
         if profit_smaller pmat.(!bmin) tmp then 
           (decale pmat !m' !bmax;
            pmat.(!bmax) <- tmp;
            incr m')
         else if profit_smaller tmp  pmat.(!bmin) then
           (for i = !m' downto !bmin do pmat.(i+1) <- pmat.(i) done;
            pmat.(!bmin) <- tmp;
            incr m')
        )
      else if equal_profits tmp  pmat.(!med) then begin
        notfound :=false;
        let i = trouve_trou pmat !med !m' in
        let suiv = if i<= !m' then pmat.(i) else pmax in
        if i <= !m' then decale pmat !m' i;
        let prec = pmat.(i-1) in
        pmat.(i) <- prec +  randomint (suiv - prec);
      end
      else if profit_smaller tmp  pmat.(!med) then
        (bmax:= !med; med:= ((!bmax + !bmin)/2))
      else (bmin := !med; med:= ((!bmax + !bmin)/2))
    done
  done;
  pmat
    
    
    
let chung m wmin  step ns=
  let w= Array.create (m+1) zerow and p = Array.create (m+1) zerop
  in
  for i = 1 to m do
    w.(i) <- add_int_weight (i-1) wmin;
    p.(i) <-  add_profits (profit_of_weight w.(i)) step
  done;
  if ns then shuffle w p;
  w,p;;

let avischung m step ns = chung m (Wandp.M.weight_of_int (1+m*(m+1))) step ns
let avissubset m ns =  avischung m  zerop ns

let notcor m wmin wmax pmin pmax=
  let w= Array.create (m+1) zerow and p = Array.create (m+1) zerop
  in
  let wsup = substract_weight wmax wmin and psup = substract_profit pmax pmin
  in
  for i = 1 to m do
    w.(i) <- add_weights wmin (rand_weight wsup);
    p.(i) <- add_profits pmin (rand_profit psup)
  done;
  w,p
    
    
let weakcor m wmin wmax  step =
  let w= Array.create (m+1) zerow and p = Array.create (m+1) zerop
  in
  let pmin = profit_unit in
  let wsup = substract_weight wmax wmin 
  in
  let twicestep = add_profits step step in
  for i = 1 to m do
    w.(i) <- add_weights wmin  (rand_weight wsup);
    p.(i) <-  max_profit pmin
        (add_profits (profit_of_weight w.(i))  
           (substract_profit (rand_profit twicestep)  step))
  done;
  w,p
    
let randcor m wmin wmax  step =
  let w= Array.create (m+1) zerow and p = Array.create (m+1) zerop
  in
  let pmin = profit_unit in
  let wsup = substract_weight wmax wmin 
  in
  for i = 1 to m do
    w.(i) <- add_weights wmin  (rand_weight wsup);
    p.(i) <- 
        let thr = profit_of_weight (div_weight_int w.(i) step) in
	let twicestep = add_profits thr thr in
        max_profit pmin
        (add_profits (profit_of_weight w.(i))  
           (substract_profit (rand_profit twicestep)  step))
  done;
  w,p
    
    
let strongcor m wmin wmax step =
  let w= Array.create (m+1) zerow and p = Array.create (m+1) zerop
  in
  let wsup = substract_weight wmax wmin 
  in
  for i = 1 to m do
    w.(i) <- add_weights wmin  (rand_weight wsup);
    p.(i) <-  add_profits (profit_of_weight w.(i))  step
  done;
  w,p
    
    
let nosimpledom m wmin wmax pmin pmax not_sorted =
  let wmat = random_incr m wmax wmin (randomint (wmax- wmin)) in
  let m' = ref 1 in
  let pmat = Array.create (m+1) zerop in
  let bsup = substract_profit pmax pmin in
  pmat.(1) <- add_profits pmin (rand_profit bsup);
  let notfound = ref true 
  and bmin = ref 1 and bmax = ref 1 and med = ref 1 
  in
  while !m' < m  do
    let tmp = add_profits pmin  (rand_profit bsup) in
    notfound :=true;
    bmin := 1;
    bmax:= !m' +1;
    med := ((!bmin + !bmax)/2);
    while !notfound do
      if  !bmax- !bmin =1  then
        (notfound := false;
         if profit_smaller pmat.(!bmin) tmp then 
           (for i = !m' downto !bmax do pmat.(i+1) <- pmat.(i) done;
            pmat.(!bmax) <- tmp;
            incr m')
         else if profit_smaller tmp  pmat.(!bmin) then
           (for i = !m' downto !bmin do pmat.(i+1) <- pmat.(i) done;
            pmat.(!bmin) <- tmp;
            incr m')
        )
      else if equal_profits tmp  pmat.(!med) then notfound :=false
      else if profit_smaller tmp  pmat.(!med) then
        (bmax:= !med; med:= ((!bmax + !bmin)/2))
      else (bmin := !med; med:= ((!bmax + !bmin)/2))
    done
  done;
  if not_sorted then shuffle wmat pmat;
  (wmat,pmat)

(* below, w and p are increasing, and more over, p.(1) > w.(1) *)
let nosimpledom2 m wmin wmax pmin pmax not_sorted=
  let wmat = random_incr m wmax wmin wmin in
  let pmin = max_profit pmin (add_profits profit_unit (profit_of_weight wmat.(1))) in
  let m' = ref 1 in
  let pmat = Array.create (m+1) zerop in
  let bsup = substract_profit pmax pmin in
  pmat.(1) <- add_profits pmin (rand_profit bsup);
  let notfound = ref true 
  and bmin = ref 1 and bmax = ref 1 and med = ref 1 
  in
  while !m' < m  do
    let tmp = add_profits pmin  (rand_profit bsup) in
    notfound :=true;
    bmin := 1;
    bmax:= !m' +1;
    med := ((!bmin + !bmax)/2);
    while !notfound do
      if  !bmax- !bmin =1  then
        (notfound := false;
         if profit_smaller pmat.(!bmin) tmp then 
           (for i = !m' downto !bmax do pmat.(i+1) <- pmat.(i) done;
            pmat.(!bmax) <- tmp;
            incr m')
         else if profit_smaller tmp  pmat.(!bmin) then
           (for i = !m' downto !bmin do pmat.(i+1) <- pmat.(i) done;
            pmat.(!bmin) <- tmp;
            incr m')
        )
      else if equal_profits tmp  pmat.(!med) then notfound :=false
      else if profit_smaller tmp  pmat.(!med) then
        (bmax:= !med; med:= ((!bmax + !bmin)/2))
      else (bmin := !med; med:= ((!bmax + !bmin)/2))
    done
  done;
  if not_sorted then  shuffle wmat pmat;
  (wmat,pmat)

let unrsubset m wmin wmax interest sort=
  let wmat = Array.create (m+1) zerow in
  let bsup = substract_weight wmax wmin in
  for i = 1 to m do wmat.(i) <- add_weights wmin (rand_weight bsup) done;
  let pmat = Array.create (m+1) zerop in
  for i = 1 to m do
    pmat.(i) <- profit_of_weight (mult_int_weight interest wmat.(i)) 
  done;
  if not sort then shuffle wmat pmat;
  wmat,pmat

let subset m wmin wmax interest sort=
  let wmat = random_incr m wmax wmin (wmin + randomint ((wmax-wmin)/m))in
  let pmat = Array.create (m+1) zerop in
  for i = 1 to m do
    pmat.(i) <- profit_of_weight (mult_int_weight interest wmat.(i)) 
  done;
  if not sort then shuffle wmat pmat;
  wmat,pmat

    
let realhard m wmin wmax=
  let pmat = Array.create (m+1) zerop in
  let wmat = random_incr m wmax wmin (randomint (wmax-wmin)) in
  for i = 1 to m do
    pmat.(i) <- profit_of_weight(add_int_weight i wmat.(i))
  done;
  shuffle wmat pmat;
  (wmat,pmat)
    
let saw m wmin wmax step =
  let wmat = random_incr m wmax wmin (randomint (wmax - wmin))in
  let pmat = Array.create (m+1) zerop in
  pmat.(1) <- add_profits (profit_of_weight wmat.(1)) step;
  for i = 2 to m do
    let pmin =  pmat.(i-1)  in
    let bound = 
      substract_profit 
        (add_profits 
           (profit_of_weight wmat.(i)) 
           (mult_int_profit (quotient_weight wmat.(i) wmat.(1)) step))
        pmin
    in
    pmat.(i) <- if equal_profits zerop bound then pmin else 
    add_profits pmin (rand_profit bound)
  done;
  shuffle wmat pmat;
  (wmat,pmat)

let unr_saw m wmin wmax pmin pmax step=
let wmat = random_warray m wmax wmin in
Array.stable_sort ( - ) wmat;
let pmat = Array.create (m+1) zerop in
  pmat.(1) <- add_profits (profit_of_weight wmat.(1)) step;
  for i = 2 to m do
    let bound = 
      min pmax
      (substract_profit 
        (add_profits 
           (profit_of_weight wmat.(i)) 
           (mult_int_profit (quotient_weight wmat.(i) wmat.(1)) step))
        pmin)
    in
    pmat.(i) <- if equal_profits zerop bound then pmin else add_profits pmin (rand_profit bound)
  done;
  shuffle wmat pmat;
  (wmat,pmat)

let harddecreasingratio m wmin wmax  pmin pmax=
  let pmat = Array.create (m+1) zerop in
  let wmat = random_incr m wmax wmin wmin in
  pmat.(1)<- add_profits pmin (rand_profit (substract_profit pmax pmin));
  for i = 2 to m do
    pmat.(i) <- max (1 + pmat.(i-1)) (truncate((float wmat.(i))*.(float pmat.(i-1))/.(float wmat.(i-1))))
  done;
  shuffle wmat pmat;
  (wmat,pmat)

let hardincreasingratio m wmin wmax pmin pmax=
  let pmat = Array.create (m+1) zerop in
  let wmat = random_incr m wmax wmin wmin in
  pmat.(1)<- add_profits pmin (rand_profit (substract_profit pmax pmin));
  for i = 2 to m do
    pmat.(i) <- truncate((float wmat.(i))*.(float pmat.(i-1))/.float wmat.(i-1)) + i - 1
  done;
  shuffle wmat pmat;
  (wmat,pmat)

let yapnosimpledom m wmin wmax pmin pmax ns =
 let w,p = nosimpledom2 m wmin wmax pmin pmax false in
 if w.(m) mod w.(1) = 0 then w.(m) <- w.(m) + rand_weight w.(1);
 let b = w.(m)/p.(m)*(p.(1)-w.(1))+w.(m) in
 if p.(m) < b then p.(m) <- b +  (rand_profit p.(1));
 let c = w.(m) + 1+ (Random.int (w.(m) mod w.(1)) -1 ) in
 if ns then shuffle w p;
 w,p,c

let nosimpledom2 m wmin wmax pmin pmax ns =
 let w = random_incr m wmax wmin wmin in
 let p = random_incr m pmax pmin pmin in
 if ns then shuffle w p;
 w,p

let randomw_without_dup n wmin wmax k =
 let step = (wmax - wmin) / k in
 let w = Array.create (n+1) wmin in
 let rwmin = ref wmin and rwmax = ref (wmin + step) in
 let rd = ref 0 in
 for i = 1 to k do
  let ws = random_incr (n/k) !rwmax !rwmin !rwmin in
  for j = 1 to (n/k) do
    if ws.(j) = 0 then Printf.printf "Bug generation j=%d" j; flush stdout;
    w.(!rd + j) <- ws.(j)
  done;
  rd := !rd + n/k ;
  rwmin := (w.(!rd ) + 1);
  rwmax := step + !rwmax
 done;
 let remain = n - !rd in
 if remain >0 then begin
  rwmax := wmax;
 let ws = random_incr remain !rwmax !rwmin !rwmin in
  for j = 1 to remain  do
    if ws.(j) = 0 then Printf.printf "Bug2 generation j=%d rwmax %d  rwmin %d\n" j !rwmax !rwmin; flush stdout;
    w.(!rd + j) <- ws.(j)
  done end;
  w

let nosimpledomstep m wmin wmax pmin pmax ns step =
 let w = randomw_without_dup m wmin wmax step in
 let p = randomw_without_dup m pmin pmax step in
 if ns then shuffle w p;
 w,p

let nosimpledomstep2 m wmin wmax pmin pmax ns step =
 let w = randomw_without_dup m wmin wmax 10 in
 let p = Array.create (m+1) zerop in
  Array.iteri 
   (fun i wi -> 
     let tmpi=
       add_profits 
	 (profit_of_weight wi) (1+ Random.int(step))
     in 
     let pi =
       if i > 1 && profit_smaller tmpi p.(i-1)  then 
         add_profits p.(i-1) (profit_of_int (1 + Random.int (step/4)))
       else tmpi
     in p.(i) <- pi
   ) 
   w ;
 if ns then shuffle w p;
 w,p

(** No common diviser for the weight array *)

let primsw m wmin wmax pmin pmax ns step =
 let wtmp = Array.of_list(0::(Eratosthen.gen wmin wmax)) in
  let k =  Array.length wtmp - 1 in
  if k < m then 
    Printf.fprintf stderr "Beware, only %d terms generated\n" k;
 let p = Array.create (k+1) zerop in
  Array.iteri 
   (fun i wi -> 
     let tmpi=
       add_profits 
	 (profit_of_weight wi) (1+ Random.int(step))
     in 
     let pi =
       if i > 1 && profit_smaller tmpi p.(i-1)  then 
         add_profits p.(i-1) (profit_of_int (1 + Random.int (step/4)))
       else tmpi
     in p.(i) <- pi
   ) 
   wtmp ;
 shuffle wtmp p;
 if k <= m then wtmp,p else Array.sub wtmp 0 (m+1),Array.sub p 0 (m+1)
