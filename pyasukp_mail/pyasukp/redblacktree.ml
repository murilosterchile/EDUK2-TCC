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

(*$Id: redblacktree.ml,v 1.2 2005/02/28 10:39:35 poirriez Exp $*)
(*redblacktree.ml*)

module Make = 
  functor 
  (M:sig
    type t
    val equivalent:  t ->  t -> bool
    val best_repres:  t ->  t -> t
    val smaller:  t ->  t -> bool
    val dominates : t -> t -> bool
  end)->
  struct
    type  t = 
      | Empty 
      | Red of  t * M.t *  t 
      | Black of  t * M.t * t
    let balance = function
      | Black(Red(Red(a,x,b),y,c),z,d) ->
          Red(Black(a,x,b),y,Black(c,z,d))
      | Black(Red(a,x,Red(b,y,c)),z,d) ->
          Red(Black(a,x,b),y,Black(c,z,d))
      | Black(a,x,Red(Red(b,y,c),z,d)) ->
          Red(Black(a,x,b),y,Black(c,z,d))
      | Black(a,x,Red(b,y,Red(c,z,d))) ->
          Red(Black(a,x,b),y,Black(c,z,d))
      | t -> t
    let insert x set = 
      let rec ins_aux = function
        | Empty ->  Red(Empty,x,Empty)
        | Red(a,y,b) as t when M.equivalent x y -> Red(a,M.best_repres x y,b)
        | Red(a,y,b) as t when M.smaller y  x -> if M.dominates y x then t else Red(a,y,ins_aux b)
        | Red(a,y,b) (*y > x*) -> Red(ins_aux a, y, b)
        | Black(a,y,b) as t when M.equivalent x y -> Black(a,M.best_repres x y,b)
        | Black(a,y,b) as t when M.smaller y x ->  if M.dominates y x then t else balance(Black(a,y,ins_aux b))
        | Black(a,y,b)  -> balance(Black(ins_aux a,y, b))
      in
      match ins_aux set with
      | Red(a,y,b) -> Black(a,y,b)
      | t -> t
            
    let rec iter f = function
      | Empty -> ()
      | Red(a,y,b) -> iter f a; f y; iter f b
      | Black(a,y,b) -> iter f a; f y; iter f b
            
    let rec fold f tr init = 
      match tr with
      | Empty -> init
      | Red(a,y,b) -> fold f b (f y (fold f a init))
      | Black(a,y,b) -> fold f b (f y (fold f a init))
  end          
module Compare = struct
  type t =  Wandp.M.item
  open Wandp.M
  let equivalent i j = equal_weights i.w j.w
  let best_repres i j = if profit_smaller i.p j.p then j else i
  let smaller  i j  = weight_smaller i.w j.w
  let dominates i j = profit_smallereq j.p i.p
end
module Set=  Make(Compare)
;;
