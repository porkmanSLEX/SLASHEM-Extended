/*	SCCS Id: @(#)dothrow.c	3.3	2000/04/16	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

/* Contains code for 't' (throw) */

#include "hack.h"

STATIC_DCL int FDECL(throw_obj, (struct obj *, int, int));
STATIC_DCL void NDECL(autoquiver);
STATIC_DCL int FDECL(gem_accept, (struct monst *, struct obj *));
STATIC_DCL int FDECL(throw_gold, (struct obj *));
STATIC_DCL void FDECL(check_shop_obj, (struct obj *,XCHAR_P,XCHAR_P,BOOLEAN_P));
STATIC_DCL void FDECL(breakobj, (struct obj *,XCHAR_P,XCHAR_P,BOOLEAN_P,BOOLEAN_P));
STATIC_DCL void FDECL(breakmsg, (struct obj *,BOOLEAN_P));
STATIC_DCL boolean FDECL(toss_up,(struct obj *, BOOLEAN_P));
STATIC_DCL boolean FDECL(throwing_weapon, (struct obj *));
STATIC_DCL void FDECL(sho_obj_return_to_u, (struct obj *obj));
static void NDECL(autoquiver);	/* KMH -- automatically fill quiver */


static NEARDATA const char toss_objs[] =
	{ ALLOW_COUNT, GOLD_CLASS, ALL_CLASSES, WEAPON_CLASS, 0 };
/* different default choices when wielding a sling (gold must be included) */
static NEARDATA const char bullets[] =
	{ ALLOW_COUNT, GOLD_CLASS, ALL_CLASSES, GEM_CLASS, 0 };

extern boolean notonhead;       /* for long worms */

#define THROW_UWEP 	1
#define THROW_USWAPWEP 	2


/* Throw the selected object, asking for direction */
STATIC_OVL int
throw_obj(obj, shotlimit, thrown)
register struct obj *obj;
int shotlimit;
int thrown;
{
	struct obj *otmp;
	struct obj *launcher;
	int multishot = 1;
	schar skill;
	long wep_mask;

	multi = 0;		/* reset; it's been used up */
	
	if (thrown == 1) launcher = uwep;
	else if (thrown == 2) launcher = uswapwep;
	else launcher = (struct obj *)0;
	
	/* ask "in what direction?" */
	if (!getdir((char *)0)) {
		if (obj->oclass == GOLD_CLASS) {
		    u.ugold += obj->quan;
		    flags.botl = 1;
		    dealloc_obj(obj);
		}
		return(0);
	}

	if(obj->oclass == GOLD_CLASS) return(throw_gold(obj));

	if(!canletgo(obj,"throw"))
		return(0);

	if(obj == uwep && welded(obj)) {
		weldmsg(obj);
		return(1);
	}

	if (obj->oartifact == ART_MJOLLNIR && obj != uwep) {
	    pline("%s must be wielded before it can be thrown.",
		The(xname(obj)));
		return(0);
	}
	if ((obj->oartifact == ART_MJOLLNIR && ACURR(A_STR) < STR19(25))
	   || (obj->otyp == BOULDER && !throws_rocks(youmonst.data))) {
		pline("It's too heavy.");
		return(1);
	}
	if(!u.dx && !u.dy && !u.dz) {
		You("cannot throw an object at yourself.");
		return(0);
	}
	if (!uarmg && !Stone_resistance && (obj->otyp == CORPSE &&
		    touch_petrifies(&mons[obj->corpsenm]))) {
		You("throw the %s corpse with your bare %s.",
		    mons[obj->corpsenm].mname, body_part(HAND));
		Sprintf(killer_buf, "%s corpse", an(mons[obj->corpsenm].mname));
		instapetrify(killer_buf);
	}
	u_wipe_engr(2);

	/* Multishot calculations
	 */
	skill = objects[obj->otyp].oc_skill;
	if ((ammo_and_launcher(obj, uwep) || skill == P_DAGGER ||
			skill == -P_DART || skill == -P_SHURIKEN) &&
		!(Confusion || Stunned)) {
	    /* Bonus if the player is proficient in this weapon... */
	    switch (P_SKILL(weapon_type(obj))) {
		    default:	break; /* No bonus */
		    case P_SKILLED:	multishot++; break;
		    case P_EXPERT:	multishot += 2; break;
	    }
	    
	    /* ...or is using a good weapon... */
	    /* Elven Craftsmanship makes for light, quick bows */
	    if (obj->otyp == ELVEN_ARROW && !obj->cursed && !rn2(3)) multishot++;
	    if (launcher && launcher->otyp == ELVEN_BOW &&
	      !launcher->cursed && !rn2(3))
		multishot++;

	    /* 1/3 of object enchantment */
	    if (launcher && launcher->spe > 1)
		multishot += (long) rounddiv(launcher->spe,3);
	    
	    /* ...or is using a special weapon for their role... */
	    switch (Role_switch) {
	    case PM_RANGER:
		multishot++;
		break;
	    case PM_ROGUE:
		if (skill == P_DAGGER) multishot++;
		break;
	    case PM_SAMURAI:
		if (obj->otyp == YA && launcher && launcher->otyp == YUMI) multishot++;
		break;
	    default:
		break;	/* No bonus */
	    }
	    /* ...or using their race's special bow */
	    switch (Race_switch) {
	    case PM_ELF:
		if (obj->otyp == ELVEN_ARROW && launcher &&
				launcher->otyp == ELVEN_BOW) multishot++;
		break;
	    case PM_ORC:
		if (obj->otyp == ORCISH_ARROW && uwep &&
				uwep->otyp == ORCISH_BOW) multishot++;
		break;
	    default:
		break;	/* No bonus */
	    }

	    if (multishot > 0) 
	    	multishot = rnd(multishot); /* Some randomness */
	    else multishot = 1;
	       
	    /* Tech: Flurry */
	    if (objects[obj->otyp].oc_skill == -P_BOW && tech_inuse(T_FLURRY)) {
		/* more than usual == volley */
		if (obj->quan >= (multishot + 1))
			You("let fly a volley of %s!", xname(obj));
		multishot += 1; /* Let'em rip! */
	    }

	    /* Shotlimit controls your rate of fire */
	    if ((shotlimit > 0) && (multishot > shotlimit)) multishot = shotlimit;

#ifdef FIREARMS
	    /* Rate of fire is intrinsic to the weapon - cannot be user selected
	     * except via altmode
	     * Only for valid launchers 
	     * (currently oc_rof conflicts with wsdam)
	     */
	    if (launcher && is_launcher(launcher))
	    {
		if (objects[(launcher->otyp)].oc_rof) 
		    multishot += (objects[(launcher->otyp)].oc_rof - 1);
		if (launcher->altmode == WP_MODE_SINGLE)
		  /* weapons switchable b/w full/semi auto */
		    multishot = 1;
		else if (launcher->altmode == WP_MODE_BURST)
		    multishot = ((multishot > 3) ? (multishot / 3) : 1);
		/* else it is auto == no change */
	    }
#endif
	}

	if (multishot < 1) multishot = 1;

	while (obj && (multishot-- > 0)) {
		wep_mask = obj->owornmask;
		/* Split this object off from its slot */
		otmp = (struct obj *)0;
		if (obj == uquiver) {
			if(obj->quan > 1L)
				setuqwep(otmp = splitobj(obj, 1L));
			else
				setuqwep((struct obj *)0);
		} else if (obj == uswapwep) {
			if(obj->quan > 1L)
				setuswapwep(otmp = splitobj(obj, 1L));
			else
				setuswapwep((struct obj *)0);
		} else if (obj == uwep) {
		    if (welded(obj)) {
			weldmsg(obj);
			return(1);
		    }
		    if (obj->quan > 1L)
 			setworn(otmp = splitobj(obj, 1L), W_WEP);
			/* not setuwep; do not change unweapon */
		    else {
			setuwep((struct obj *)0);
			if (uwep) return(1); /* unwielded, died, rewielded */
		    }
		} else if(obj->quan > 1L)
			otmp = splitobj(obj, 1L);

		freeinv(obj);
		throwit(obj, wep_mask, thrown);
		obj = otmp;
	}	/* while (multishot) */
	return (1);
}


int
dothrow()
{
	register struct obj *obj;
	int oldmulti = multi, result, shotlimit;
	char *oldsave_cm = save_cm;

	/*
	 * Since some characters shoot multiple missiles at one time,
	 * allow user to specify a count prefix for 'f' or or select 
	 * a number of items in the item selection for 't' to limit
	 * number of items thrown (to avoid possibly hitting something
	 * behind target after killing it, or perhaps to conserve ammo).
	 *
	 * Nethack 3.3.0 uses prefixes for all - should this revert to that?
	 *
	 * Prior to 3.3.0, command ``3t'' meant ``t(shoot) t(shoot) t(shoot)''
	 * and took 3 turns.  Now it means ``t(shoot at most 3 missiles)''.
	 */

	if(check_capacity((char *)0)) return(0);
	obj = getobj(uslinging() ? bullets : toss_objs, "throw");
	/* it is also possible to throw food */
	/* (or jewels, or iron balls... ) */

	if (!obj) return(0);

	/* kludge to work around parse()'s pre-decrement of 'multi' */
	shotlimit = (multi || save_cm) ? multi + 1 : 0;

        result = throw_obj(obj, shotlimit, THROW_UWEP);
        
        multi = oldmulti;
        save_cm = oldsave_cm;
        return (result);
}


/* KMH -- automatically fill quiver */
/* Suggested by Jeffrey Bay <jbay@convex.hp.com> */
static void
autoquiver()
{
	register struct obj *otmp, 
			    *oammo = (struct obj *) 0, 
			    *ospear = (struct obj *) 0, 
			    *omissile = (struct obj *) 0, 			    
			    *omisc = (struct obj *) 0;


	if (uquiver)
		return;

	/* Scan through the inventory */
	for (otmp = invent; otmp; otmp = otmp->nobj) {
		if (otmp->owornmask || otmp->oartifact || !otmp->dknown) {
			;	/* Skip it */
		} else if (otmp->otyp == ROCK ||
			/* seen rocks or known flint or known glass */
			(objects[otmp->otyp].oc_name_known &&
			 otmp->otyp == FLINT) ||
			(objects[otmp->otyp].oc_name_known &&
			 otmp->oclass == GEM_CLASS &&
			 objects[otmp->otyp].oc_material == GLASS)) {
			if (uslinging())
			    oammo = otmp;
			else if (!omisc)
			    omisc = otmp;
		} else if (otmp->oclass == GEM_CLASS) {
			;	/* skip non-rock gems--they're ammo but
				   player has to select them explicitly */
		} else if (is_ammo(otmp)) {
			if (ammo_and_launcher(otmp, uwep)) {
				/* Ammo matched with launcher (bow and arrow, crossbow and bolt) */
				oammo = otmp;
				continue;
			}		
		} else if (is_missile(otmp)) {
			/* Missile (dart, shuriken, etc.) */
			omissile = otmp;
		} else if (otmp->oclass == WEAPON_CLASS && !is_launcher(otmp)) {
			/* Ordinary weapon */
			omisc = otmp;
		}
	}

	/* Pick the best choice */
	if (oammo)
		setuqwep(oammo);
	else if (ospear)
		setuqwep(ospear);
	else if (omissile)
		setuqwep(omissile);
	else if (omisc)
		setuqwep(omisc);

	return;
}

int
dofire()
{
	int result, shotlimit;

	if(check_capacity((char *)0)) return(0);
	if (!uquiver) {
		if (!flags.autoquiver) {
			/* Don't automatically fill the quiver */
			You("have no ammunition readied!");
			return(dothrow());
		}
		autoquiver();
		if (!uquiver) {
			You("have nothing appropriate for your quiver!");
			return(dothrow());
		} else {
			You("fill your quiver:");
			prinv((char *)0, uquiver, 0L);
		}
	}

	/*
	 * Since some characters shoot multiple missiles at one time,
	 * allow user to specify a count prefix for 'f' or 't' to limit
	 * number of items thrown (to avoid possibly hitting something
	 * behind target after killing it, or perhaps to conserve ammo).
	 *
	 * The number specified can never increase the number of missiles.
	 * Using ``5f'' when the shooting skill (plus RNG) dictates launch
	 * of 3 projectiles will result in 3 being shot, not 5.
	 */
	/* kludge to work around parse()'s pre-decrement of `multi' */
	shotlimit = (multi || save_cm) ? multi + 1 : 0;
	multi = 0;		/* reset; it's been used up */

	if (u.twoweap) {
		if (!can_twoweapon()) untwoweapon();
		else if (ammo_and_launcher(uquiver,uwep) 
		    && ammo_and_launcher(uquiver, uswapwep)){
			result = throw_obj(uquiver, shotlimit, THROW_UWEP);
			if ((result == 1) && uquiver) 
			    result += throw_obj(uquiver, shotlimit, THROW_USWAPWEP);
			result--;
			return(result);
		}
	}
	result = (throw_obj(uquiver, shotlimit, THROW_UWEP));
	
	return result;
}


/*
 * Object hits floor at hero's feet.  Called from drop() and throwit().
 */
void
hitfloor(obj)
register struct obj *obj;
{
	if (IS_SOFT(levl[u.ux][u.uy].typ) || u.uinwater) {
		dropy(obj);
		return;
	}
	if (IS_ALTAR(levl[u.ux][u.uy].typ))
		doaltarobj(obj);
	else
		pline("%s hit%s the %s.", Doname2(obj),
		      (obj->quan == 1L) ? "s" : "", surface(u.ux,u.uy));

	if (hero_breaks(obj, u.ux, u.uy, TRUE)) return;
	if (ship_object(obj, u.ux, u.uy, FALSE)) return;
	dropy(obj);
}

/*
 * Walk a path from src_cc to dest_cc, calling a proc for each location
 * except the starting one.  If the proc returns FALSE, stop walking
 * and return FALSE.  If stopped early, dest_cc will be the location
 * before the failed callback.
 */
boolean
walk_path(src_cc, dest_cc, check_proc, arg)
    coord *src_cc;
    coord *dest_cc;
    boolean FDECL((*check_proc), (genericptr_t, int, int));
    genericptr_t arg;
{
    int x, y, dx, dy, x_change, y_change, err, i, prev_x, prev_y;
    boolean keep_going = TRUE;

    /* Use Bresenham's Line Algorithm to walk from src to dest */
    dx = dest_cc->x - src_cc->x;
    dy = dest_cc->y - src_cc->y;
    prev_x = x = src_cc->x;
    prev_y = y = src_cc->y;

    if (dx < 0) {
	x_change = -1;
	dx = -dx;
    } else
	x_change = 1;
    if (dy < 0) {
	y_change = -1;
	dy = -dy;
    } else
	y_change = 1;

    i = err = 0;
    if (dx < dy) {
	while (i++ < dy) {
	    prev_x = x;
	    prev_y = y;
	    y += y_change;
	    err += dx;
	    if (err >= dy) {
		x += x_change;
		err -= dy;
	    }
	/* check for early exit condition */
	if (!(keep_going = (*check_proc)(arg, x, y)))
	    break;
	}
    } else {
	while (i++ < dx) {
	    prev_x = x;
 	    prev_y = y;
	    x += x_change;
	    err += dy;
	    if (err >= dx) {
		y += y_change;
		err -= dx;
	    }
	/* check for early exit condition */
	if (!(keep_going = (*check_proc)(arg, x, y)))
	    break;
	}
    }

    if (keep_going)
	return TRUE;	/* successful */

    dest_cc->x = prev_x;
    dest_cc->y = prev_y;
    return FALSE;
}

/*
 * Single step for the hero flying through the air from jumping, flying,
 * etc.  Called from hurtle() and jump() via walk_path().  We expect the
 * argument to be a pointer to an integer -- the range -- which is
 * used in the calculation of points off it we hit something.
 *
 * Bumping into monsters won't cause damage but will wake them and make
 * them angry.  Auto-pickup isn't done, since you don't have control over
 * your movements at the time.
 *
 * Possible additions/changes:
 *	o really attack monster if we hit one
 *	o set stunned if we hit a wall or door
 *	o reset nomul when we stop
 *	o creepy feeling if pass through monster (if ever implemented...)
 *	o bounce off walls
 *	o let jumps go over boulders
 */
boolean
hurtle_step(arg, x, y)
    genericptr_t arg;
    int x, y;
{
    int ox, oy, *range = (int *)arg;
    struct obj *obj;
    struct monst *mon;
    boolean may_pass = TRUE;

    if (!isok(x,y)) {
	You_feel("the spirits holding you back.");
	return FALSE;
    }

    if (!Passes_walls || !(may_pass = may_passwall(x, y))) {
	if (IS_ROCK(levl[x][y].typ) || closed_door(x,y)) {
	    pline("Ouch!");
	    losehp(rnd(2+*range), IS_ROCK(levl[x][y].typ) ?
		   "bumping into a wall" : "bumping into a door", KILLED_BY);
	    return FALSE;
	}
	if (levl[x][y].typ == IRONBARS) {
	    You("crash into some iron bars.  Ouch!");
	    losehp(rnd(2+*range), "crashing into iron bars", KILLED_BY);
	    return FALSE;
	}
	if ((obj = sobj_at(BOULDER,x,y)) != 0) {
	    You("bump into a %s.  Ouch!", xname(obj));
	    losehp(rnd(2+*range), "bumping into a boulder", KILLED_BY);
	    return FALSE;
	}
	if (!may_pass) {
	    /* did we hit a no-dig non-wall position? */
	    You("smack into something!");
	    losehp(rnd(2+*range), "touching the edge of the universe", KILLED_BY);
	    return FALSE;
	}
    }

    if ((mon = m_at(x, y)) != 0) {
	You("bump into %s.", a_monnam(mon));
	wakeup(mon);
	return FALSE;
    }

    ox = u.ux;
    oy = u.uy;
    u.ux = x;
    u.uy = y;
    newsym(ox, oy);		/* update old position */
    vision_recalc(1);		/* update for new position */
    flush_screen(1);
    if (--*range < 0)		/* make sure our range never goes negative */
	*range = 0;
    if (*range != 0)
	delay_output();
    return TRUE;
}

/*
 * The player moves through the air for a few squares as a result of
 * throwing or kicking something.
 *
 * dx and dy should be the direction of the hurtle, not of the original
 * kick or throw and be only.
 */
void
hurtle(dx, dy, range, verbose)
    int dx, dy, range;
    boolean verbose;
{
    coord uc, cc;

    /* The chain is stretched vertically, so you shouldn't be able to move
     * very far diagonally.  The premise that you should be able to move one
     * spot leads to calculations that allow you to only move one spot away
     * from the ball, if you are levitating over the ball, or one spot
     * towards the ball, if you are at the end of the chain.  Rather than
     * bother with all of that, assume that there is no slack in the chain
     * for diagonal movement, give the player a message and return.
     */
    if(Punished && !carried(uball)) {
	You_feel("a tug from the iron ball.");
	nomul(0);
	return;
    } else if (u.utrap) {
	You("are anchored by the %s.",
	    u.utraptype == TT_WEB ? "web" : u.utraptype == TT_LAVA ? "lava" :
		u.utraptype == TT_INFLOOR ? surface(u.ux,u.uy) : "trap");
	nomul(0);
	return;
    }

    /* make sure dx and dy are [-1,0,1] */
    dx = sgn(dx);
    dy = sgn(dy);

    if(!range || (!dx && !dy) || u.ustuck) return; /* paranoia */

    nomul(-range);
    if (verbose)
	You("%s in the opposite direction.", range > 1 ? "hurtle" : "float");
    if (In_sokoban(&u.uz))
	change_luck(-1);	/* Sokoban guilt */
    uc.x = u.ux;
    uc.y = u.uy;
    /* this setting of cc is only correct if dx and dy are [-1,0,1] only */
    cc.x = u.ux + (dx * range);
    cc.y = u.uy + (dy * range);
    (void) walk_path(&uc, &cc, hurtle_step, (genericptr_t)&range);
}

STATIC_OVL void
check_shop_obj(obj, x, y, broken)
register struct obj *obj;
register xchar x, y;
register boolean broken;
{
	struct monst *shkp = shop_keeper(*u.ushops);

	if(!shkp) return;

	if(broken) {
		if (obj->unpaid) {
		    (void)stolen_value(obj, u.ux, u.uy,
				       (boolean)shkp->mpeaceful, FALSE);
		    subfrombill(obj, shkp);
		}
		obj->no_charge = 1;
		return;
	}

	if (!costly_spot(x, y) || *in_rooms(x, y, SHOPBASE) != *u.ushops) {
		/* thrown out of a shop or into a different shop */
		if (obj->unpaid) {
		    (void)stolen_value(obj, u.ux, u.uy,
				       (boolean)shkp->mpeaceful, FALSE);
		    subfrombill(obj, shkp);
		}
	} else {
		if (costly_spot(u.ux, u.uy) && costly_spot(x, y)) {
		    if(obj->unpaid) subfrombill(obj, shkp);
		    else if(!(x == shkp->mx && y == shkp->my))
			    sellobj(obj, x, y);
		}
	}
}

/*
 * Hero tosses an object upwards with appropriate consequences.
 *
 * Returns FALSE if the object is gone.
 */
STATIC_OVL boolean
toss_up(obj, hitsroof)
struct obj *obj;
boolean hitsroof;
{
    const char *almost;
    /* note: obj->quan == 1 */

    if (hitsroof) {
	if (breaktest(obj)) {
		pline("%s hits the %s.", Doname2(obj), ceiling(u.ux, u.uy));
		breakmsg(obj, !Blind);
		breakobj(obj, u.ux, u.uy, TRUE, TRUE);
		return FALSE;
	}
	almost = "";
    } else {
	almost = " almost";
    }
    pline("%s%s hits the %s, then falls back on top of your %s.",
	  Doname2(obj), almost, ceiling(u.ux,u.uy), body_part(HEAD));

    /* object now hits you */

     if (obj->oclass == POTION_CLASS) {
 	potionhit(&youmonst, obj, TRUE);
     } else if (breaktest(obj)) {
 	int otyp = obj->otyp, ocorpsenm = obj->corpsenm;
	int blindinc;

	breakmsg(obj, !Blind);
	breakobj(obj, u.ux, u.uy, TRUE, TRUE);
 	obj = 0;	/* it's now gone */
	switch (otyp) {
 	case EGG:
		if (touch_petrifies(&mons[ocorpsenm]) &&
		    !uarmh && !Stone_resistance &&
		    !(poly_when_stoned(youmonst.data) && polymon(PM_STONE_GOLEM)))
 		goto petrify;
 	case CREAM_PIE:
 	case BLINDING_VENOM:
		pline("You've got it all over your %s!", body_part(FACE));
 		blindinc = rnd(25);
 		if (blindinc && !Blindfolded) {
 		    if (otyp != BLINDING_VENOM)
 			u.ucreamed += blindinc;
 		    else if (!Blind)
 			pline("It blinds you!");
 		    make_blinded(Blinded + blindinc, FALSE);
 		}
 		break;
 	default:
 		break;
	}
	return FALSE;
     } else {		/* neither potion nor other breaking object */
 	boolean less_damage = uarmh && is_metallic(uarmh);
 	int dmg = dmgval(obj, &youmonst);
 
 	if (!dmg) {	/* probably wasn't a weapon; base damage on weight */
 	    dmg = (int) obj->owt / 100;
 	    if (dmg < 1) dmg = 1;
 	    else if (dmg > 6) dmg = 6;
	    if (youmonst.data == &mons[PM_SHADE] &&
 		    objects[obj->otyp].oc_material != SILVER)
 		dmg = 0;
 	}
 	if (dmg > 1 && less_damage) dmg = 1;
 	if (dmg > 0) dmg += u.udaminc;
 	if (dmg < 0) dmg = 0;	/* beware negative rings of increase damage */
 
 	if (uarmh) {
 	    if (less_damage && dmg < (Upolyd ? u.mh : u.uhp))
			pline("Fortunately, you are wearing a hard helmet.");
 	    else if (flags.verbose &&
		    !(obj->otyp == CORPSE && touch_petrifies(&mons[obj->corpsenm])))
			Your("%s does not protect you.", xname(uarmh));
	} else if (obj->otyp == CORPSE && touch_petrifies(&mons[obj->corpsenm])) {
	    if (!Stone_resistance &&
		    !(poly_when_stoned(youmonst.data) && polymon(PM_STONE_GOLEM))) {
 petrify:
 		killer_format = KILLED_BY;
 		killer = "elementary physics";	/* "what goes up..." */
 		You("turn to stone.");
 		if (obj) dropy(obj);	/* bypass most of hitfloor() */
 		done(STONING);
 		return obj ? TRUE : FALSE;
 	    }
 	}
 	hitfloor(obj);
 	losehp(dmg, "falling object", KILLED_BY_AN);
    }
    return TRUE;
}

/* return true for weapon meant to be thrown; excludes ammo */
STATIC_OVL boolean
throwing_weapon(obj)
struct obj *obj;
{
	return (is_missile(obj) || is_spear(obj) ||
			/* daggers and knife (excludes scalpel) */
			(is_blade(obj) && (objects[obj->otyp].oc_dir & PIERCE)) ||
			/* special cases [might want to add AXE] */
			obj->otyp == WAR_HAMMER || obj->otyp == AKLYS);
}

/* the currently thrown object is returning to you (not for boomerangs) */
STATIC_OVL void
sho_obj_return_to_u(obj)
struct obj *obj;
{
    /* might already be our location (bounced off a wall) */
    if (bhitpos.x != u.ux || bhitpos.y != u.uy) {
	int x = bhitpos.x - u.dx, y = bhitpos.y - u.dy;

	tmp_at(DISP_FLASH, obj_to_glyph(obj));
	while(x != u.ux || y != u.uy) {
	    tmp_at(x, y);
	    delay_output();
	    x -= u.dx; y -= u.dy;
	}
	tmp_at(DISP_END, 0);
    }
}

void
throwit(obj, wep_mask, thrown)
register struct obj *obj;
long wep_mask;	/* used to re-equip returning boomerang */
int thrown;
{
	register struct monst *mon;
	register int range, urange;
	register struct obj *launcher = (struct obj*) 0;
	boolean impaired = (Confusion || Stunned || Blind ||
			   Hallucination || Fumbling);

	if (thrown == 1) launcher = uwep;
	else if (thrown == 2) launcher = uswapwep;
	
	/* KMH -- Handle Plague here */
	if (launcher && launcher->oartifact == ART_PLAGUE &&
			ammo_and_launcher(obj, launcher) && is_poisonable(obj))
		obj->opoisoned = 1;

	if ((obj->cursed || obj->greased) && (u.dx || u.dy) && !rn2(7)) {
	    boolean slipok = TRUE;
	    if (ammo_and_launcher(obj, launcher))
			pline("%s misfires!", The(xname(obj)));
	    else {
		if (obj->greased || throwing_weapon(obj))
 		    /* BUG: this message is grammatically incorrect if obj has
 		       a plural name; greased gloves or boots for instance. */
		    pline("%s slips as you throw it!", The(xname(obj)));
		else slipok = FALSE;
	    }
	    if (slipok) {
		u.dx = rn2(3)-1;
		u.dy = rn2(3)-1;
		if (!u.dx && !u.dy) u.dz = 1;
		impaired = TRUE;
	    }
	}

	if(u.uswallow) {
		mon = u.ustuck;
		bhitpos.x = mon->mx;
		bhitpos.y = mon->my;
	} else if(u.dz) {
	    if (u.dz < 0 && Role_if(PM_VALKYRIE) &&
		    obj->oartifact == ART_MJOLLNIR && !impaired) {
		pline("%s hits the %s and returns to your hand!",
		      The(xname(obj)), ceiling(u.ux,u.uy));
		obj = addinv(obj);
		(void) encumber_msg();
		setuwep(obj);
/*            if (!fire_weapon) setuwep(obj);                
            else setuqwep(obj);*/
	    } else if (u.dz < 0 && !Is_airlevel(&u.uz) &&
		    !Underwater && !Is_waterlevel(&u.uz)) {
		(void) toss_up(obj, rn2(5));
	    } else {
		hitfloor(obj);
	    }
	    return;

	} else if(obj->otyp == BOOMERANG && !Underwater) {
		if(Is_airlevel(&u.uz) || Levitation)
		    hurtle(-u.dx, -u.dy, 1, TRUE);
		mon = boomhit(u.dx, u.dy);
		if(mon == &youmonst) {          /* the thing was caught */
			exercise(A_DEX, TRUE);
			obj = addinv(obj);
			(void) encumber_msg();
			if (wep_mask && !(obj->owornmask & wep_mask))
			    setworn(obj, wep_mask);
			return;
		}
	} else {
		urange = (int)(ACURRSTR)/2;
 		/* balls are easy to throw or at least roll */
 		/* also, this insures the maximum range of a ball is greater
 		 * than 1, so the effects from throwing attached balls are
 		 * actually possible
 		 */
 		if (obj->otyp == HEAVY_IRON_BALL)
 			range = urange - (int)(obj->owt/100);
 		else
 			range = urange - (int)(obj->owt/40);
		if (obj == uball) {
			if (u.ustuck) range = 1;
			else if (range >= 5) range = 5;
		}
		if (range < 1) range = 1;

		/* KMH, balance patch -- new macros */
		if (is_ammo(obj)) {
		    if (ammo_and_launcher(obj, launcher)) {
#ifdef FIREARMS
			if (is_launcher(launcher) && 
					objects[(launcher->otyp)].oc_range) 
				range = objects[(launcher->otyp)].oc_range;
			else
#endif
				range++;
		    } else {
			range /= 2;
		    }
		}

		if (Is_airlevel(&u.uz) || Levitation) {
		    /* action, reaction... */
		    urange -= range;
		    if(urange < 1) urange = 1;
		    range -= urange;
		    if(range < 1) range = 1;
		}

		if (obj->otyp == BOULDER)
		    range = 20;         /* you must be giant */
		else if (obj->oartifact == ART_MJOLLNIR)
		    range = (range + 1) / 2;    /* it's heavy */
		else if (obj == uball && u.utrap && u.utraptype == TT_INFLOOR)
		    range = 1;

		if (Underwater) range = 1;

		mon = bhit(u.dx,u.dy,range,THROWN_WEAPON,
			   (int FDECL((*),(MONST_P,OBJ_P)))0,
			   (int FDECL((*),(OBJ_P,OBJ_P)))0,
			   obj);

		/* have to do this after bhit() so u.ux & u.uy are correct */
		if(Is_airlevel(&u.uz) || Levitation)
		    hurtle(-u.dx, -u.dy, urange, TRUE);
	}	

	if(mon) {
		boolean obj_gone;

		if (mon->isshk &&
			obj->where == OBJ_MINVENT && obj->ocarry == mon)
		    return;		/* alert shk caught it */
		(void) snuff_candle(obj);
		notonhead = (bhitpos.x != mon->mx || bhitpos.y != mon->my);
		obj_gone = thitmonst(mon, obj, thrown);
		/* Monster may have been tamed; this frees old mon */
		mon = m_at(bhitpos.x, bhitpos.y);

		/* [perhaps this should be moved into thitmonst or hmon] */
		if (mon && mon->isshk &&
			(!inside_shop(u.ux, u.uy) ||
			 !index(in_rooms(mon->mx, mon->my, SHOPBASE), *u.ushops)))
		    hot_pursuit(mon);

		if (obj_gone) {
#ifdef FIREARMS
			/* Detonate rockets */
			if (is_grenade(obj)) {
				grenade_explode(obj->otyp, bhitpos.x, bhitpos.y, TRUE, FALSE);
			} else if (launcher && is_ammo(obj) && (objects[obj->otyp].oc_dir & EXPLOSION)
			   && ammo_and_launcher(obj, launcher)) {
			    	if (cansee(bhitpos.x,bhitpos.y)) 
			    		pline("%s explodes in a ball of fire!", Doname2(obj));
			    	else You("hear an explosion");
				explode(bhitpos.x, bhitpos.y, ZT_SPELL(ZT_FIRE), d(3,8), WEAPON_CLASS);
			}
#endif
			
			return;
		}
	}

#ifdef FIREARMS
	/* Handle grenades or rockets */
	if (is_grenade(obj)) {
		arm_bomb(obj, TRUE);
	} else if (launcher && is_ammo(obj) && (objects[obj->otyp].oc_dir & EXPLOSION)
	   && ammo_and_launcher(obj, launcher)) {
	    	if (cansee(bhitpos.x,bhitpos.y)) 
	    		pline("%s explodes in a ball of fire!", Doname2(obj));
	    	else You("hear an explosion");
		explode(bhitpos.x, bhitpos.y, ZT_SPELL(ZT_FIRE), d(3,8), WEAPON_CLASS);
	}

	if (is_bullet(obj) && (ammo_and_launcher(obj, launcher) && !is_grenade(obj))) {
		check_shop_obj(obj, bhitpos.x,bhitpos.y, TRUE);
		obfree(obj, (struct obj *)0);
		return;
	}
#endif

	if (u.uswallow) {
		/* ball is not picked up by monster */
		if (obj != uball) (void) mpickobj(u.ustuck,obj);
	} else {
		/* the code following might become part of dropy() */
		int obj_glyph = obj_to_glyph(obj);

		if (obj->oartifact == ART_MJOLLNIR &&
			Role_if(PM_VALKYRIE) && rn2(100)) {
		    /* we must be wearing Gauntlets of Power to get here */
		    sho_obj_return_to_u(obj);	    /* display its flight */

		    if (!impaired && rn2(100)) {
			pline("%s returns to your hand!", The(xname(obj)));
			obj = addinv(obj);
			(void) encumber_msg();
                        setuwep(obj);
/*                        if (!fire_weapon) setuwep(obj);
                        else setuqwep(obj);*/
			if(cansee(bhitpos.x, bhitpos.y))
			    newsym(bhitpos.x,bhitpos.y);
		    } else {
			int dmg = rnd(4);
			if (Blind)
			    pline("%s hits your %s!",
				  The(xname(obj)), body_part(ARM));
			else
			    pline("%s flies back toward you, hitting your %s!",
				  The(xname(obj)), body_part(ARM));
			(void) artifact_hit((struct monst *) 0, &youmonst,
					    obj, &dmg, 0);
			losehp(dmg, xname(obj), KILLED_BY);
			if(ship_object(obj, u.ux, u.uy, FALSE))
			    return;
			dropy(obj);
		    }
		    return;
		}

		if (!IS_SOFT(levl[bhitpos.x][bhitpos.y].typ) &&
			breaktest(obj)) {
		    tmp_at(DISP_FLASH, obj_to_glyph(obj));
		    tmp_at(bhitpos.x, bhitpos.y);
		    delay_output();
		    tmp_at(DISP_END, 0);
		    breakmsg(obj, cansee(bhitpos.x, bhitpos.y));
		    breakobj(obj, bhitpos.x, bhitpos.y, TRUE, TRUE);
		    return;
		}
		if(flooreffects(obj,bhitpos.x,bhitpos.y,"fall")) return;
		if (obj->otyp == CRYSKNIFE && (!obj->oerodeproof || !rn2(10))) {
		    obj->otyp = WORM_TOOTH;
		    obj->oerodeproof = 0;
		}
		if (mon && mon->isshk && is_pick(obj)) {
		    if (cansee(bhitpos.x, bhitpos.y))
			pline("%s snatches up %s.",
			      Monnam(mon), the(xname(obj)));
		    if(*u.ushops)
			check_shop_obj(obj, bhitpos.x, bhitpos.y, FALSE);
		    (void) mpickobj(mon, obj);	/* may merge and free obj */
		    return;
		}
		(void) snuff_candle(obj);
		if (!mon && ship_object(obj, bhitpos.x, bhitpos.y, FALSE))
		    return;
		place_object(obj, bhitpos.x, bhitpos.y);
		if(*u.ushops && obj != uball)
		    check_shop_obj(obj, bhitpos.x, bhitpos.y, FALSE);

		stackobj(obj);
		if (obj == uball)
		    drop_ball(bhitpos.x, bhitpos.y);
		if (cansee(bhitpos.x, bhitpos.y))
		    newsym(bhitpos.x,bhitpos.y);
		if (obj_sheds_light(obj))
		    vision_full_recalc = 1;
	}
}

/* an object may hit a monster; various factors adjust the chance of hitting */
int
omon_adj(mon, obj, mon_notices)
struct monst *mon;
struct obj *obj;
boolean mon_notices;
{
	int tmp = 0;

	/* size of target affects the chance of hitting */
	tmp += (mon->data->msize - MZ_MEDIUM);          /* -2..+5 */
	/* sleeping target is more likely to be hit */
	if (mon->msleeping) {
	    tmp += 2;
	    if (mon_notices) mon->msleeping = 0;
	}
	/* ditto for immobilized target */
	if (!mon->mcanmove || !mon->data->mmove) {
	    tmp += 4;
	    if (mon_notices && mon->data->mmove && !rn2(10)) {
		mon->mcanmove = 1;
		mon->mfrozen = 0;
	    }
	}
	/* some objects are more likely to hit than others */
	switch (obj->otyp) {
	case HEAVY_IRON_BALL:
	    if (obj != uball) tmp += 2;
	    break;
	case BOULDER:
	    tmp += 6;
	    break;
	default:
	    if (obj->oclass == WEAPON_CLASS || is_weptool(obj) ||
		    obj->oclass == GEM_CLASS)
		tmp += hitval(obj, mon);
	    break;
	}
	return tmp;
}

/* thrown object misses target monster */
STATIC_OVL void
tmiss(obj, mon)
struct obj *obj;
struct monst *mon;
{
    miss(xname(obj), mon);
    if (!rn2(3)) wakeup(mon);
    return;
}

#define quest_arti_hits_leader(obj,mon)	\
  (obj->oartifact && is_quest_artifact(obj) && (mon->data->msound == MS_LEADER))

/*
 * Object thrown by player arrives at monster's location.
 * Return 1 if obj has disappeared or otherwise been taken care of,
 * 0 if caller must take care of it.
 */
int
thitmonst(mon, obj, thrown)
register struct monst *mon;
register struct obj   *obj;
int thrown;
{
	register int    tmp; /* Base chance to hit */
	register int    disttmp; /* distance modifier */
	struct obj *launcher;
	
	int otyp = obj->otyp;
	boolean guaranteed_hit = (u.uswallow && mon == u.ustuck);

	/* Differences from melee weapons:
	 *
	 * Dex still gives a bonus, but strength does not.
	 * Polymorphed players lacking attacks may still throw.
	 * There's a base -1 to hit.
	 * No bonuses for fleeing or stunned targets (they don't dodge
	 *    melee blows as readily, but dodging arrows is hard anyway).
	 * Not affected by traps, etc.
	 * Certain items which don't in themselves do damage ignore tmp.
	 * Distance and monster size affect chance to hit.
	 */
	/* KMH -- added ring of increase accuracy */

	if (thrown == 1) launcher = uwep;
	else if (thrown == 2) launcher = uswapwep;

	tmp = -1 + Luck + find_mac(mon) + u.uhitinc +
			maybe_polyd(youmonst.data->mlevel, u.ulevel);
	if (ACURR(A_DEX) < 4) tmp -= 3;
	else if (ACURR(A_DEX) < 6) tmp -= 2;
	else if (ACURR(A_DEX) < 8) tmp -= 1;
	else if (ACURR(A_DEX) >= 14) tmp += (ACURR(A_DEX) - 14);

	/* Modify to-hit depending on distance; but keep it sane.
	 * Polearms get a distance penalty even when wielded; it's
	 * hard to hit at a distance.
	 */
	disttmp = 3 - distmin(u.ux, u.uy, mon->mx, mon->my);
	if(disttmp < -4) disttmp = -4;
	tmp += disttmp;

	/* gloves are a hinderance to proper use of bows */
	if (uarmg && launcher && objects[launcher->otyp].oc_skill == P_BOW) {
	    switch (uarmg->otyp) {
	    case GAUNTLETS_OF_POWER:    /* metal */
		tmp -= 2;
		break;
	    case GAUNTLETS_OF_FUMBLING:
		tmp -= 3;
		break;
	    case LEATHER_GLOVES:
	    case GAUNTLETS_OF_SWIMMING:            
	    case GAUNTLETS_OF_DEXTERITY:
		break;
	    default:
		impossible("Unknown type of gloves (%d)", uarmg->otyp);
		break;
	    }
	}

	tmp += omon_adj(mon, obj, TRUE);
	if (is_orc(mon->data) && maybe_polyd(is_elf(youmonst.data),
			Race_if(PM_ELF)))
	    tmp++;
	if (guaranteed_hit) {
	    tmp += 1000; /* Guaranteed hit */
	}

	/* sleeping unicorns don't catch gems */
	if (obj->oclass == GEM_CLASS && is_unicorn(mon->data) && !mon->msleeping) {
	    if (mon->mtame) {
		pline("%s catches and drops %s.", Monnam(mon), the(xname(obj)));
		return 0;
	    } else {
		pline("%s catches %s.", Monnam(mon), the(xname(obj)));
		return gem_accept(mon, obj);
	    }
	}

	/* don't make game unwinnable if naive player throws artifact
	   at leader.... */
	if (quest_arti_hits_leader(obj, mon)) {
	    /* not wakeup(), which angers non-tame monsters */
	    mon->msleeping = 0;
	    mon->mstrategy &= ~STRAT_WAITMASK;

	    if (mon->mcanmove) {
		pline("%s catches %s.", Monnam(mon), the(xname(obj)));
		if (mon->mpeaceful) {
		    boolean next2u = monnear(mon, u.ux, u.uy);

		    finish_quest(obj);	/* acknowledge quest completion */
		    pline("%s %s %s back to you.", Monnam(mon),
			  (next2u ? "hands" : "tosses"), the(xname(obj)));
		    if (!next2u) sho_obj_return_to_u(obj);
		    obj = addinv(obj);	/* back into your inventory */
		    (void) encumber_msg();
		} else {
		    /* angry leader caught it and isn't returning it */
		    (void) mpickobj(mon, obj);
		}
		return 1;		/* caller doesn't need to place it */
	    }
	    return(0);
	}

	if (obj->oclass == WEAPON_CLASS || is_weptool(obj) ||
		obj->oclass == GEM_CLASS) {
	    if (is_ammo(obj)) {
		if (!ammo_and_launcher(obj, launcher)) {
		    tmp -= 4;
		} else {
		    tmp += launcher->spe - greatest_erosion(launcher);
		    tmp += weapon_hit_bonus(launcher);
		    /*
		     * Elves and Samurais are highly trained w/bows,
		     * especially their own special types of bow.
		     * Polymorphing won't make you a bow expert.
		     */
		    if ((Race_if(PM_ELF) || Race_if(PM_DROW) ||
		    		Role_if(PM_SAMURAI)) &&
				objects[launcher->otyp].oc_skill == P_BOW) {
			tmp++;
			if (is_elf(youmonst.data) && launcher->otyp == ELVEN_BOW) tmp++;
			else if (Race_if(PM_DROW) && launcher->otyp == DARK_ELVEN_BOW) tmp++;
			else if (Role_if(PM_SAMURAI) && launcher->otyp == YUMI) tmp++;
		    }
		}
	    } else {
		if (otyp == BOOMERANG)		/* arbitrary */
			tmp += 4;
		else if (throwing_weapon(obj))	/* meant to be thrown */
		    tmp += 2;
		else				/* not meant to be thrown */
		    tmp -= 2;
		/* we know we're dealing with a weapon or weptool handled
		   by WEAPON_SKILLS once ammo objects have been excluded */
		tmp += weapon_hit_bonus(obj);
	    }
#ifdef DEBUG
	    pline("(%i/20)", tmp);
#endif

	    if (tmp >= rnd(20)) {
		if (hmon(mon,obj,thrown)) {  /* mon still alive */
		    cutworm(mon, bhitpos.x, bhitpos.y, obj);
		}
		exercise(A_DEX, TRUE);

		/* Detonate bolts shot by Hellfire */
		if (launcher && (launcher->oartifact == ART_HELLFIRE)
		    && is_ammo(obj) && ammo_and_launcher(obj, launcher)) {
		    	if (cansee(bhitpos.x,bhitpos.y)) 
		    		pline("%s explodes in a ball of fire!", Doname2(obj));
		    	else You("hear an explosion");
			explode(bhitpos.x, bhitpos.y, ZT_SPELL(ZT_FIRE), d(2,6), WEAPON_CLASS);
		}

		/* projectiles other than magic stones
		 * sometimes disappear when thrown
		 * WAC - Spoon always disappears after doing damage
		 */
                if ((((objects[otyp].oc_skill < P_NONE &&
                		objects[otyp].oc_skill > -P_BOOMERANG) ||
			(obj->oclass == GEM_CLASS && !objects[otyp].oc_magic))
                    && rn2(3))
# ifdef P_SPOON
                    || (obj->oartifact == ART_HOUCHOU)
# endif
			/* WAC catch Hellfire */
			|| (launcher && launcher->oartifact == ART_HELLFIRE 
			&& is_ammo(obj) && ammo_and_launcher(obj, launcher))
                    ) {
		    if (*u.ushops)
			check_shop_obj(obj, bhitpos.x,bhitpos.y, TRUE);
		    obfree(obj, (struct obj *)0);
		    return 1;
		}
	    } else {
		tmiss(obj, mon);
	    }

	} else if (otyp == HEAVY_IRON_BALL) {
	    exercise(A_STR, TRUE);
	    if (tmp >= rnd(20)) {
		int was_swallowed = guaranteed_hit;

		exercise(A_DEX, TRUE);
		if (!hmon(mon,obj,thrown)) {         /* mon killed */
		    if (was_swallowed && !u.uswallow && obj == uball)
			return 1;       /* already did placebc() */
		}
	    } else {
		tmiss(obj, mon);
	    }

	} else if (otyp == BOULDER) {
	    exercise(A_STR, TRUE);
	    if (tmp >= rnd(20)) {
		exercise(A_DEX, TRUE);
		(void) hmon(mon,obj,thrown);
	    } else {
		tmiss(obj, mon);
	    }

 	} else if ((otyp == EGG || otyp == CREAM_PIE ||
 		    otyp == BLINDING_VENOM || otyp == ACID_VENOM) &&
 		(guaranteed_hit || ACURR(A_DEX) > rnd(25))) {
	    (void) hmon(mon, obj, thrown);
	    return 1;   /* hmon used it up */

  	} else if (obj->oclass == POTION_CLASS &&
  		(guaranteed_hit || ACURR(A_DEX) > rnd(25))) {
	    potionhit(mon, obj, TRUE);
	    return 1;

	} else if (obj->oclass == FOOD_CLASS &&
		   is_domestic(mon->data) && tamedog(mon,obj)) {
	    return 1;           /* food is gone */
	} else {
	    if (guaranteed_hit) {
		/* this assumes that guaranteed_hit is due to swallowing */
		pline("%s vanishes into %s %s.",
		    The(xname(obj)), s_suffix(mon_nam(mon)),
		    is_animal(u.ustuck->data) ? "entrails" : "currents");
		wakeup(mon);
	    } else {
		tmiss(obj, mon);
	    }
	    
	    /* [Tom] Dorothy wants more pets... */        
	    if(obj->otyp == BANANA && mon->data->mlet == S_YETI)
		if(tamedog(mon,obj)) return(1);
	    if(obj->otyp == CHEESE && mon->data == &mons[PM_GIANT_RAT])
		if(tamedog(mon,obj)) return(1);
	    if(obj->otyp == CHEESE && mon->data == &mons[PM_SEWER_RAT])
		if(tamedog(mon,obj)) return(1);
	    if(obj->otyp == CHEESE && mon->data == &mons[PM_BLACK_RAT])
		if(tamedog(mon,obj)) return(1);
	    if(obj->otyp == CHEESE && mon->data == &mons[PM_PACK_RAT])
		if(tamedog(mon,obj)) return(1);
	    if(obj->otyp == CARROT && mon->data == &mons[PM_RABBIT])
		if(tamedog(mon,obj)) return(1);
	    if(obj->otyp == CARROT && mon->data == &mons[PM_RABID_RABBIT])
		if(tamedog(mon,obj)) return(1);
	}
	return 0;
}

STATIC_OVL int
gem_accept(mon, obj)
register struct monst *mon;
register struct obj *obj;
{
	char buf[BUFSZ];
	boolean is_buddy = sgn(mon->data->maligntyp) == sgn(u.ualign.type);
	boolean is_gem = objects[obj->otyp].oc_material == GEMSTONE;
	int ret = 0;
	static NEARDATA const char nogood[] = " is not interested in your junk.";
	static NEARDATA const char acceptgift[] = " accepts your gift.";
	static NEARDATA const char maybeluck[] = " hesitatingly";
	static NEARDATA const char noluck[] = " graciously";
	static NEARDATA const char addluck[] = " gratefully";

	Strcpy(buf,Monnam(mon));
	mon->mpeaceful = 1;

	/* object properly identified */
	if(obj->dknown && objects[obj->otyp].oc_name_known) {
		if(is_gem) {
			if(is_buddy) {
				Strcat(buf,addluck);
				change_luck(5);
			} else {
				Strcat(buf,maybeluck);
				change_luck(rn2(7)-3);
			}
		} else {
			Strcat(buf,nogood);
			goto nopick;
		}
	/* making guesses */
	} else if(obj->onamelth || objects[obj->otyp].oc_uname) {
		if(is_gem) {
			if(is_buddy) {
				Strcat(buf,addluck);
				change_luck(2);
			} else {
				Strcat(buf,maybeluck);
				change_luck(rn2(3)-1);
			}
		} else {
			Strcat(buf,nogood);
			goto nopick;
		}
	/* value completely unknown to @ */
	} else {
		if(is_gem) {
			if(is_buddy) {
				Strcat(buf,addluck);
				change_luck(1);
			} else {
				Strcat(buf,maybeluck);
				change_luck(rn2(3)-1);
			}
		} else {
			Strcat(buf,noluck);
		}
	}
	Strcat(buf,acceptgift);
	if(*u.ushops) check_shop_obj(obj, mon->mx, mon->my, TRUE);
	(void) mpickobj(mon, obj);	/* may merge and free obj */
	ret = 1;

nopick:
	if(!Blind) pline("%s", buf);
	if (!tele_restrict(mon)) rloc(mon);
	return(ret);
}

/*
 * Comments about the restructuring of the old breaks() routine.
 *
 * There are now three distinct phases to object breaking:
 *     breaktest() - which makes the check/decision about whether the
 *                   object is going to break.
 *     breakmsg()  - which outputs a message about the breakage,
 *                   appropriate for that particular object. Should
 *                   only be called after a positve breaktest().
 *                   on the object and, if it going to be called,
 *                   it must be called before calling breakobj().
 *                   Calling breakmsg() is optional.
 *     breakobj()  - which actually does the breakage and the side-effects
 *                   of breaking that particular object. This should
 *                   only be called after a positive breaktest() on the
 *                   object.
 *
 * Each of the above routines is currently static to this source module.
 * There are two routines callable from outside this source module which
 * perform the routines above in the correct sequence.
 *
 *   hero_breaks() - called when an object is to be broken as a result
 *                   of something that the hero has done. (throwing it,
 *                   kicking it, etc.)
 *   breaks()      - called when an object is to be broken for some
 *                   reason other than the hero doing something to it.
 */

/*
 * The hero causes breakage of an object (throwing, dropping it, etc.)
 * Return 0 if the object didn't break, 1 if the object broke.
 */
int
hero_breaks(obj, x, y, from_invent)
struct obj *obj;
xchar x, y;             /* object location (ox, oy may not be right) */
boolean from_invent;    /* thrown or dropped by player; maybe on shop bill */
{
	boolean in_view = !Blind;
	if (!breaktest(obj)) return 0;
	breakmsg(obj, in_view);
	breakobj(obj, x, y, TRUE, from_invent);
	return 1;
}

/*
 * The object is going to break for a reason other than the hero doing
 * something to it.
 * Return 0 if the object doesn't break, 1 if the object broke.
 */
int
breaks(obj, x, y)
struct obj *obj;
xchar x, y;             /* object location (ox, oy may not be right) */
{
	boolean in_view = Blind ? FALSE : cansee(x, y);

	if (!breaktest(obj)) return 0;
	breakmsg(obj, in_view);
	breakobj(obj, x, y, FALSE, FALSE);
	return 1;
}

/*
 * Unconditionally break an object. Assumes all resistance checks
 * and break messages have been delivered prior to getting here.
 * This routine assumes the cause is the hero if heros_fault is TRUE.
 *
 */
STATIC_OVL void
breakobj(obj, x, y, heros_fault, from_invent)
struct obj *obj;
xchar x, y;             /* object location (ox, oy may not be right) */
boolean heros_fault;
boolean from_invent;
{
	switch (obj->oclass == POTION_CLASS ? POT_WATER : obj->otyp) {
		case MIRROR:
			if (heros_fault)
			    change_luck(-2);
			break;
		case POT_WATER:         /* really, all potions */
			if (obj->otyp == POT_OIL && obj->lamplit) {
			    splatter_burning_oil(x,y);
			} else if (distu(x,y) <= 2) {
			    /* [what about "familiar odor" when known?] */
			    if (obj->otyp != POT_WATER)
				You("smell a peculiar odor...");
			    potionbreathe(obj);
			}
			/* monster breathing isn't handled... [yet?] */
			break;
		case EGG:
			/* breaking your own eggs is bad luck */
			if (heros_fault && obj->spe && obj->corpsenm >= LOW_PM)
			    change_luck((schar) -min(obj->quan, 5L));
			break;
	}
	if (heros_fault) {
	    if (from_invent) {
		if (*u.ushops)
			check_shop_obj(obj, x, y, TRUE);
	    } else if (!obj->no_charge && costly_spot(x, y)) {
		/* it is assumed that the obj is a floor-object */
		char *o_shop = in_rooms(x, y, SHOPBASE);
		struct monst *shkp = shop_keeper(*o_shop);

		if (shkp) {             /* (implies *o_shop != '\0') */
		    static NEARDATA long lastmovetime = 0L;
		    static NEARDATA boolean peaceful_shk = FALSE;
		    /*  We want to base shk actions on her peacefulness
			at start of this turn, so that "simultaneous"
			multiple breakage isn't drastically worse than
			single breakage.  (ought to be done via ESHK)  */
		    if (moves != lastmovetime)
			peaceful_shk = shkp->mpeaceful;
		    if (stolen_value(obj, x, y, peaceful_shk, FALSE) > 0L &&
			(*o_shop != u.ushops[0] || !inside_shop(u.ux, u.uy)) &&
			moves != lastmovetime) make_angry_shk(shkp, x, y);
		    lastmovetime = moves;
		}
	    }
	}
	delobj(obj);
}

/*
 * Check to see if obj is going to break, but don't actually break it.
 * Return 0 if the object isn't going to break, 1 if it is.
 */
boolean
breaktest(obj)
struct obj *obj;
{
	if (obj_resists(obj, 1, 99)) return 0;
	switch (obj->oclass == POTION_CLASS ? POT_WATER : obj->otyp) {
		case MIRROR:
		case CRYSTAL_BALL:
		/* KMH, balance patch -- removed to prevent abuse
		case ORB_OF_DESTRUCTION:                        
		case ORB_OF_ENCHANTMENT:
		case ORB_OF_CHARGING:*/
#ifdef TOURIST
		case EXPENSIVE_CAMERA:
#endif
		case POT_WATER:         /* really, all potions */
		case EGG:
		case CREAM_PIE:
		case ACID_VENOM:
		case BLINDING_VENOM:
			return 1;
		default:
			return 0;
	}
}

STATIC_OVL void
breakmsg(obj, in_view)
struct obj *obj;
boolean in_view;
{
	const char *to_pieces;

	to_pieces = "";
	switch (obj->oclass == POTION_CLASS ? POT_WATER : obj->otyp) {
		case MIRROR:
		case CRYSTAL_BALL:
#ifdef TOURIST
		case EXPENSIVE_CAMERA:
#endif
			to_pieces = " into a thousand pieces";
			/*FALLTHRU*/
		case POT_WATER:         /* really, all potions */
			if (!in_view)
			    You_hear("%s shatter!", something);
			else
			    pline("%s shatter%s%s!", Doname2(obj),
				(obj->quan==1) ? "s" : "", to_pieces);
			break;
		case EGG:
			pline("Splat!");
			break;
		case CREAM_PIE:
			if (in_view) pline("What a mess!");
			break;
		case ACID_VENOM:
		case BLINDING_VENOM:
			pline("Splash!");
			break;
	}
}

/*
 *  Note that the gold object is *not* attached to the fobj chain.
 */
STATIC_OVL int
throw_gold(obj)
struct obj *obj;
{
	int range, odx, ody;
	long zorks = obj->quan;
	register struct monst *mon;

	if(u.uswallow) {
		pline(is_animal(u.ustuck->data) ?
			"%s in the %s's entrails." : "%s into %s.",
			"The gold disappears", mon_nam(u.ustuck));
		u.ustuck->mgold += zorks;
		dealloc_obj(obj);
		return(1);
	}

	if(u.dz) {
		if (u.dz < 0 && !Is_airlevel(&u.uz) &&
					!Underwater && !Is_waterlevel(&u.uz)) {
	pline_The("gold hits the %s, then falls back on top of your %s.",
		    ceiling(u.ux,u.uy), body_part(HEAD));
		    /* some self damage? */
		    if(uarmh) pline("Fortunately, you are wearing a helmet!");
		}
		bhitpos.x = u.ux;
		bhitpos.y = u.uy;
	} else {
		/* consistent with range for normal objects */
		range = (int)((ACURRSTR)/2 - obj->owt/40);

		/* see if the gold has a place to move into */
		odx = u.ux + u.dx;
		ody = u.uy + u.dy;
		if(!ZAP_POS(levl[odx][ody].typ) || closed_door(odx, ody)) {
			bhitpos.x = u.ux;
			bhitpos.y = u.uy;
		} else {
			mon = bhit(u.dx, u.dy, range, THROWN_WEAPON,
				   (int FDECL((*),(MONST_P,OBJ_P)))0,
				   (int FDECL((*),(OBJ_P,OBJ_P)))0,
				   obj);
			if(mon) {
			    if (ghitm(mon, obj))        /* was it caught? */
				return 1;
			} else {
			    if(ship_object(obj, bhitpos.x, bhitpos.y, FALSE))
				return 1;
			}
		}
	}

	if(flooreffects(obj,bhitpos.x,bhitpos.y,"fall")) return(1);
	if(u.dz > 0)
		pline_The("gold hits the %s.", surface(bhitpos.x,bhitpos.y));
	place_object(obj,bhitpos.x,bhitpos.y);
	if(*u.ushops) sellobj(obj, bhitpos.x, bhitpos.y);
	stackobj(obj);
	newsym(bhitpos.x,bhitpos.y);
	return(1);
}

/*dothrow.c*/
