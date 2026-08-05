(***************************************************************************)
(*                                  PYAsUKP                                *)
(*    PYAsUKP: Yet Another solver (for the) Unbounded Knapsack Problem     *)
(*           Vincent Poirriez with Nicola Yanev and Rumen Andonov          *)
(*                          LAMIH-ROI UMR CNRS 8530                        *)
(*  Copyright (C) 1998-2005  Vincent Poirriez				   *)
(* <vincent Dot poirriez At univ-valenciennes Dot fr>                      *)
(*   Distributed only by permission.                                       *)
(*   This program is free software; See the file LICENSE  for precise      *)
(*   descriptions of the license.                                          *)
(***************************************************************************)

(* $Id: eduk.ml,v 1.7 2005/04/27 13:09:48 poirriez Exp $ *)
(*eduk.ml*)

(** The core of the algorithm *)
open Wandp.M
open Globals
open Init
open Bounds
open Parsecl
open Datastruct
open Datastructtype
open Datastructtype.BOUNDS
open Sequences
  
(** The first phase of the algorithm has to compute all the optimal values for
   the knapsacks with capacities below the weight of the heaviest not-dominated item
   type. This phase is called reduction.
   
   We have choosen to compute the reduction phase by layers, each layer it-self is
   divided in slices.
   
   The layers are subdivisions of the initial weight range [\[0..wmax\]]. The layer's
   initialisation phase selects the item types with weight that fit in the layer's
   range. While this require to examine all the remaining item types, we also
   compute some simple dominance test, specially if the best item type already
   considered has changed in the previous layer.
   The main computation within a layer is to compute slices (see the module [Slice]).
   We decide to define the sequence of the slices upperbounds as the sequence
   of the weights of the selected item types.
   
   At the end of each slice we compute a threshold dominance test.
 *)
  
let check_threshold_dominance w capacity =
  Chainlist.delete_cond_all
    (fun ji -> 
      Dominance.threshold_test 
        (Astore.M.get iteminfos.last_contribution ji)
        w.(Astore.M.get iteminfos.item ji) capacity)
    decreasingS
    
let heavyer_not_th_dom w =
  let wmax = ref Wandp.M.zerow in
  Chainlist.iter 
    (fun ji-> let wi= w.(Astore.M.get iteminfos.item ji) in
    if Wandp.M.weight_smaller !wmax wi then wmax := wi)
    decreasingS;
  !wmax
    
    
let last_used_is_the_best bound kpt =
  Astore.M.get iteminfos.item kpt =  ( bound.b1).i
    
let transfert_in_sequence_result seq until_w =
  let continue = ref (not(Seq.isempty seq)) in
  match !allbound with
  | None -> 
      while !continue do
        let ((wpt,ppt,kpt) as next) = Seq.peek seq in
        if Wandp.M.weight_smaller until_w wpt then continue := false
        else begin
          Seq.junk seq;
          Seq.put sequence_result next;
          Astore.M.change iteminfos.last_contribution kpt wpt;
          continue := not(Seq.isempty seq)
        end
      done
  | Some bound -> 
      while !continue do
        let ((wpt,ppt,kpt) as next) = Seq.peek seq in
        if Wandp.M.weight_smaller until_w wpt then continue := false
        else begin
          if 
            (last_used_is_the_best bound kpt ||
            not (Bounds.is_context_dominated bound 
                   (!(Init.rwith_wp) bound wpt ppt kpt))) 
          then begin
            Seq.put sequence_result next;
            Astore.M.change iteminfos.last_contribution kpt wpt
          end;
	  if !trace then  begin 
	    Printf.printf " y = %d p = %d z = %d U = %d\n"  wpt ppt !(bound.z) (bound.u);
	    flush stdout;
	  end;
          Seq.junk seq;
          continue := not (Seq.isempty seq)
        end
      done
        
let below_item_type w p hslice item (binf, pacc) =
  let rec sub_slice pacc bsup binf slice_height  =
    let (li,lj) = Seq.item_in sequence_result in
    let remain_height = Wandp.M.substract_weight bsup binf in
    if Wandp.M.weight_smallereq remain_height slice_height then
      let r,seq =  Slice.one w p pacc bsup in
      begin
        transfert_in_sequence_result seq bsup;
        if !trace then Seq.print_seq_from sequence_result (li,lj);
        check_threshold_dominance w bsup;
        r
      end 
    else
      let newbinf = Wandp.M.add_weights binf slice_height in
      let (_, newpacc),seq = 
        Slice.one w p pacc newbinf
      in 
      begin
        transfert_in_sequence_result seq newbinf;
        if !trace then Seq.print_seq_from sequence_result (li,lj);
        check_threshold_dominance w newbinf;
        sub_slice newpacc bsup newbinf slice_height
      end
  in
  let newpacc = 
    if Chainlist.is_empty decreasingS then pacc else
    snd(sub_slice pacc item.w binf hslice)
  in
  let lastwpt,lastppt,_ = Seq.lastval sequence_result in
  let (li,lj) = Seq.item_in sequence_result in
  let introduced, optp = introduce w  (lastwpt,newpacc) item in
  if !trace then Seq.print_seq_from sequence_result (li,lj);
  check_threshold_dominance w item.w;
  (item.w, optp)
    
let layer w p selected wpopt hslice =
  Prepro.fold_left_to
    (below_item_type w p hslice)
    selected wpopt
    
let layers w p wmax wpopt items hslice =
  let rec compute  ((wacc,pacc) as wpopt) remains=
    match  Select.next_lightest items remains !nb2select pacc  with
    | None -> wpopt
    | Some (selected, newremains) ->
        let (currentc,_) as new_wpopt = 
          layer w p selected wpopt hslice 
        in
        compute new_wpopt newremains
  in compute wpopt [(0,Array.length items -1)]
    
let reduction w p c hslice wb wab =
  let items,n, wimin1, imin1, wmax, imax,  bests=
    structures w p c wb wab in
  nb_layers := min (max !nb_layers ((Array.length w)/10)) ((wmax-wimin1)/10);
  let items =  
    match !bound with None -> items
    | Some bound ->
        let nbdel =  Bandbukp2.solve items nbbests bound c
  in
        if nbdel = 0 then items else
        Array.sub items 0 (n - nbdel)
  in
  let wpopt1 = init imin1  in
  if !trace then Seq.print_seq sequence_result;
  let ibest = 
    if Chainlist.is_empty decreasingS then 
      let (b1,_,_) = bests in b1.i 
    else
      Astore.M.get iteminfos.item (Chainlist.hd decreasingS) in
  let actual_hslice = Wandp.M.max_weight hslice wimin1 in
  let wpopt = 
    layers w p wmax wpopt1 items actual_hslice
  in
  (wpopt, w.(ibest))
    
let rec standard w p c c' layer_height wb (wacc, pacc) binf =
  let period_level_reached = Chainlist.is_single decreasingS in
  if  period_level_reached  || weight_smallereq c binf then 
    period_level_reached, wacc, pacc, if period_level_reached then binf else c
  else
    let newc = 
      if weight_smallereq c' binf && !not_reached_c' then begin
        not_reached_c' := false;
        Init.rwith_wp := 
          (Bounds.bound_up_half_c 
             {cp =  Seq.lastval sequence_result;
              stbpt= Seq.lastin sequence_result}) ;
        let wmax = heavyer_not_th_dom w in
        min_weight c (add_weights wmax binf) 
      end
      else c
    in
    let bsup = min_weight newc (add_weights layer_height binf) in
    let (li,lj) = Seq.item_in sequence_result in
    let (_,newpacc) as newwpacc,seq = Slice.one w p pacc bsup  in
    transfert_in_sequence_result seq bsup;
    if !trace then Seq.print_seq_from sequence_result (li,lj);
    check_threshold_dominance w bsup;
    standard w p newc c' layer_height  wb newwpacc bsup
      
let fill_with_best w p c best ijkbest =
  let wopt,popt,kopt = Seq.lastval sequence_result in
  let diff = substract_weight c wopt in
  let rest, nb_best = mod_quot_weight diff w.(best) in
  let ((targetw,targetp,targetk), itarget), nb = 
    if rest = zerow then 
      ((wopt,popt,kopt),Seq.lastin sequence_result), nb_best else
      let weight_target = substract_weight wopt (substract_weight w.(best) rest) 
      in
      Seq.search_down1 
        (fun w -> weight_smallereq w weight_target) sequence_result, nb_best+1
  in
  let nbbest =   if targetk = ijkbest then
    quotient_weight (substract_weight c targetw) w.(best)
  else nb
  in 
  (add_weights targetw (mult_int_weight nbbest w.(best)),
   add_profits targetp (mult_int_profit nbbest p.(best)), ijkbest),
  nbbest, itarget
    
let search_max_in_window w c ((wi,pi,_) as lastval,lastind) =
  let wmax = heavyer_not_th_dom w in
  let fit = ( >= ) (Wandp.M.substract_weight wi wmax) in
  let start_comp = Seq.search_down1_from fit sequence_result lastind in
  Seq.search_max_two_ways sequence_result c start_comp lastval
    
let forward w p c with_bounds with_all_bounds =
  try
    let hslice = !layer_height in
    let wpreduction, wbest = 
      reduction w p c  hslice with_bounds with_all_bounds in
    let rwmin = ref wbest in
    Chainlist.iter 
      (fun (i,j) -> let ind = (Astore.M.get iteminfos.item (i,j)) in
      if Wandp.M.weight_smaller w.(ind) !rwmin then 
        rwmin := w.(ind)) decreasingS;
    let wreduct = fst wpreduction in
    let c'' = if c mod 2 = 0 then c/2 else 1+ c/2 in
    let c' = max wreduct c'' in
    let period_level_reached, last_w_comp, pacc,lastc = 
      standard w p c c' !rwmin with_bounds  wpreduction wreduct 
    in
    let ijkbest = (Chainlist.hd decreasingS) in
    let best = Astore.M.get iteminfos.item ijkbest in
    let wpopt,nb_best,starting_backtrack_point =
      if period_level_reached then fill_with_best w p c best ijkbest
      else Seq.lastval sequence_result, 0, Seq.lastin sequence_result 
    in
    Normal (period_level_reached,
            wpopt, 
            ijkbest, 
            nb_best, 
            starting_backtrack_point, lastc, wreduct)
  with
  | BOUNDS.Optimal (e,cp,s) -> With_bound (e,cp,s)
        
let rec backtrack w p starting_backtrack_point nbi sol =
  Seq.jumpout  sequence_result starting_backtrack_point;
  let (wsbp,psbp,ksbp) = Seq.peek sequence_result in
  if wsbp=zerow then sol else
  let wtarget = substract_weight wsbp w.(Astore.M.get iteminfos.item ksbp) 
  and ptarget = substract_weight psbp p.(Astore.M.get iteminfos.item ksbp) in
  let ((_ , _,targetk) as twp, itarget) = 
    Seq.search_down1_from 
      (fun w -> weight_smallereq w wtarget) sequence_result starting_backtrack_point
  in
  if itarget = Seq.zero_index then  (Astore.M.get iteminfos.item ksbp, nbi + 1) :: sol
  else if targetk = ksbp then  backtrack w p itarget (nbi + 1) sol 
  else backtrack w p itarget 0 ((Astore.M.get iteminfos.item ksbp, nbi + 1) :: sol)
      
let agregat sol =
  List.map (fun (i,r) -> i, !r)
    (List.fold_left 
       (fun l (i,nb) -> 
         try 
           (if nb > 0 then let r = List.assoc i l in r := nb + !r); 
           l
         with Not_found -> (i, ref nb) :: l) 
       [] sol)
    
let backtrack_with_bound w p ( bound, ((wpt,ppt,ipt) as cp), sol) =
  let dp_built_sol =
    match sol with 
    | [] -> begin 
        match ipt with (-1, k) when k > 0 ->  [(k,1)] 
        | (-1,_) -> []
        | (-2,k) -> (* La solution optimale est calculée avec le plus petit item type*)
            [(k,Wandp.M.quotient_weight wpt w.(k))]
        | _ ->  begin
            Seq.put sequence_result cp;
            backtrack w p (Seq.lastin sequence_result) 0 []
        end
    end
    |  _ -> sol
  in
  if sol = [] then
    match bound.misc with
    | MT _ | Both {sb = Smt} ->
        let bests = [ bound.b1; b2 bound.misc; b3 bound.misc] in
        fst (List.fold_left 
               (fun (sol,cap) i -> 
                 let (remcap, nb) = Wandp.M.mod_quot_weight cap w.(i.i) in 
                 (i.i,nb)::sol,remcap)
               (dp_built_sol,bound.c) bests)
    | V m | Both {v=m;sb=_} -> let imin = m.imin in
      let xb_imin = quotient_weight bound.c imin.w in
      (imin.i, xb_imin)::dp_built_sol
    | Uphc m -> backtrack w p m.stbpt 0 dp_built_sol
          
  else sol
      
let accumulate w p sol =
  List.fold_left 
    (fun (cw,cp) (i,nb) -> ((add_weights cw (mult_int_weight nb w.(i))),
                            (add_profits cp (mult_int_profit nb p.(i)))))
    (zerow,zerop)
    sol
    
(*
   [accumulate w p sol] compute the sums of the capacitys and the profits of
   the pairs [(i,nb)] in [sol].
 *)
    
let rebuildsol w p c resforward = 
  match resforward with
  | Normal (pl,wpopt, 
            best, 
            nb_best, 
            starting_backtrack_point, 
            last_w_comp, wreduct) when pl || last_w_comp = c ->
              let initsol = if nb_best = 0 then [] else
              [(Astore.M.get iteminfos.item best,nb_best)] in
                agregat(backtrack w p starting_backtrack_point 0 initsol)
  | Normal (pl,wpopt, 
            best, 
            nb_best, 
            starting_backtrack_point, 
            last_w_comp, wreduct) -> 
   let stbpt0,stbpt1,popt= 
     search_max_in_window w c (wpopt,starting_backtrack_point) in
        agregat (backtrack w p stbpt0 0 ((backtrack w p stbpt1 0 [])))
  | With_bound ( bound, ((wpt,ppt,ipt) as cp), sol) -> 
      agregat(backtrack_with_bound w p ( bound, cp, sol))
                
  
(**
   {% \paragraph{The functions to be used by external programs}\mbox{}\\ %}
 *)
let resolve_ w p c wb wab =
  Datastruct.refresh();
  match  forward w p c wb wab with
  | Normal (pl,wpopt, 
            best, 
            nb_best, 
            starting_backtrack_point, 
            last_w_comp, wreduct) ->
              begin
                Seq.jumpout sequence_result starting_backtrack_point;
                let lastcp = Seq.peek sequence_result in
                let initsol = if nb_best = 0 then [] else [(Astore.M.get iteminfos.item best,nb_best)] in
                let sol = 
                  agregat(backtrack w p starting_backtrack_point 0 initsol)
                in
                let (optimal_cap,optimal_profit) = accumulate w p sol 
                in
                (sol, optimal_cap, optimal_profit)    
              end
  | With_bound (bound, cp,sol) ->
      let sol = agregat(backtrack_with_bound w p ( bound, cp,sol)  )
      in
      let (optimal_cap,optimal_profit) = accumulate w p sol in
      (sol, optimal_cap, optimal_profit)    

(** Solver do not assume anything on global parameters. *)
let solver  ?(wb = true) ?(wab = true) ?(wmt = false) ?(zhbr = true) ?(dm = false) ?(nbmaxsol = 10000) ?(nbbests = (-1)) ?(pp = true) ?(tr = false) w p c =
  Globals.nbmaxsol := nbmaxsol;
  Globals.prepro := pp;
  Globals.trace := tr;
  Globals.nbbests := nbbests;  
  Globals.dm := dm;
  Globals.zhbr := zhbr;
  Globals.mt := wmt;  
  resolve_ w p c wb wab

let resolve w p c = resolve_ w p c true true
        
let resolve_array  w p c =
  match forward w p c true true with
  | Normal (pl,wpopt, 
            best, 
            nb_best, 
            starting_backtrack_point, 
            last_w_comp, wreduct ) ->
              begin
                Seq.jumpout sequence_result starting_backtrack_point;
                let lastcp = Seq.peek sequence_result in
                let initsol = 
                  if nb_best = 0 then [] else [(Astore.M.get iteminfos.item best,nb_best)] in
                let sol = 
                  agregat(backtrack w p starting_backtrack_point 0 initsol)
                in
                let (optimal_cap,optimal_profit) = accumulate w p sol 
                in
                let isol,nbsol = List.split sol in
                (Array.of_list isol,
                 Array.of_list nbsol, optimal_cap, optimal_profit)
              end
  | With_bound (item ,bound, sol) ->
      let sol = agregat( backtrack_with_bound w p (item, bound,sol)  )
      in
      let (optimal_cap,optimal_profit) = accumulate w p sol in
      let isol,nbsol = List.split sol in
      (Array.of_list isol, Array.of_list nbsol, optimal_cap, optimal_profit)

