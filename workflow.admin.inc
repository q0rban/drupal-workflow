<?php
// $Id: workflow.admin.inc,v 1.7.2.3.2.6 2010/09/02 21:13:18 q0rban Exp $

/**
 * @file
 * Administrative pages for configuring workflows.
 */

/**
 * Form builder. Create the form for adding a workflow.
 */
function workflow_add_form(&$form_state) {
  $form = array();
  $form['label'] = array(
    '#type' => 'textfield',
    '#title' => t('Workflow Label'),
    '#required' => TRUE,
    '#maxlength' => '254',
    '#width' => 120,
  );
  $form['name'] = array(
    '#type' => 'textfield',
    '#title' => t('Machine Name'),
    '#required' => TRUE,
    '#description' => t('Enter the machine name for this workflow. Must be unique and use only alphanumeric characters and underscores.'),
    '#maxlength' => '254',
  );
  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Add Workflow'),
  );
  return $form;
}

/**
 * Validate the workflow add form.
 *
 * @see workflow_add_form()
 */
function workflow_add_form_validate($form, &$form_state) {
  $form_state['values']['name'] = strtolower($form_state['values']['name']);
  $values = $form_state['values'];

  // Make sure workflow name is not a duplicate.
  if (db_result(db_query("SELECT COUNT(*) FROM {workflows} WHERE label = '%s'", $values['label']))) {
    form_set_error('label', t('A workflow with the name %name already exists. Please enter another name for your new workflow.',
      array('%name' => $values['label'])));
  }

  // Make sure machine name contains only alnum chars and underscores.
  if (preg_match("/[^[:alnum:]_]/", $values['name'])) {
    form_set_error('name', t("The machine name must consist of alphanumeric or underscore characters only."));
  }
  // Make sure workflow name is not a duplicate.
  if (db_result(db_query("SELECT COUNT(*) FROM {workflows} WHERE name = '%s'", $values['name']))) {
    form_set_error('name', t('A workflow with the machine name %name already exists. Please enter another name for your new workflow.',
      array('%name' => $values['name'])));
  }
  // Make sure the workflow name isn't associated with disabled states.
  if (db_result(db_query("SELECT COUNT(*) FROM {workflow_states} WHERE workflow_name = '%s'", $values['name']))) {
    form_set_error('name', t('It appears this machine name has previously disabled states. Please enter another name for your new workflow.',
      array('%name' => $values['name'])));
  }
}

/**
 * Submit handler for the workflow add form.
 *
 * @see workflow_add_form()
 */
function workflow_add_form_submit($form, &$form_state) {
  $values = $form_state['values'];

  $workflow['label'] = $values['label'];
  $workflow['name'] = strtolower($values['name']);

  if ($workflow = workflow_save($workflow)) {
    watchdog('workflow', 'Created workflow %label', array('%label' => $workflow->label));
    drupal_set_message(t('The workflow %label was created. You should now add states to your workflow.', array('%label' => $workflow->label)), 'status');
    $form_state['redirect'] = 'admin/build/workflow/edit/'. $workflow->name .'/states';
  }
  else {
    watchdog('workflow', 'There was an error creating workflow %label', array('%label' => $values['label']), WATCHDOG_WARNING);
    drupal_set_message(t('There was a problem saving this workflow. Please contact your site administrator.'), 'warning');
  }
  workflow_invalidate_cache();
}

/**
 * Form for confirmation of workflow deletion.
 *
 * @param $name
 *   The machine name of the workflow to delete.
 * @return
 *   Confirmation form array.
 */
function workflow_delete_form(&$form_state, $name) {
  $form['name'] = array(
    '#type' => 'value',
    '#value' => $name,
  );
  return confirm_form(
    $form,
    t('Are you sure you want to delete %title? All nodes that have a workflow state associated with this workflow will have those workflow states removed.', array('%title' => workflow_get_label($name))),
    !empty($_GET['destination']) ? $_GET['destination'] : 'admin/build/workflow',
    t('This action cannot be undone.'),
    t('Delete'),
    t('Cancel')
  );
}

/**
 * Submit handler for workflow deletion form.
 *
 * @see workflow_delete_form()
 */
function workflow_delete_form_submit($form, &$form_state) {
  if ($form_state['values']['confirm'] == 1) {
    $name = $form_state['values']['name'];
    $label = workflow_get_label($name);
    workflow_deletewf($name);
    watchdog('workflow', 'Deleted workflow %label with all its states', array('%label' => $label));
    drupal_set_message(t('The workflow %label with all its states was deleted.', array('%label' => $label)));
    $form_state['redirect'] = 'admin/build/workflow';
  }
}

/**
 * Form for confirmation of workflow revert.
 *
 * @param $name
 *   The machine name of the workflow to revert.
 * @return
 *   Confirmation form array.
 */
function workflow_revert_form(&$form_state, $name) {
  $form['name'] = array(
    '#type' => 'value',
    '#value' => $name,
  );
  return confirm_form(
    $form,
    t('Are you sure you want to revert %title? Configuration will switch to the one provided by code.', array('%title' => workflow_get_label($name))),
    !empty($_GET['destination']) ? $_GET['destination'] : 'admin/build/workflow',
    t('This action cannot be undone.'),
    t('Revert'),
    t('Cancel')
  );
}


/**
 * Submit handler for workflow revert form.
 *
 * @see workflow_revert_form()
 */
function workflow_revert_form_submit($form, &$form_state) {
  if ($form_state['values']['confirm'] == 1) {
    $name = $form_state['values']['name'];
    $label = workflow_get_label($name);
    workflow_export_crud_delete($name);
    watchdog('workflow', 'Reverted workflow %label with all its states', array('%label' => $label));
    drupal_set_message(t('The workflow %label with all its states was reverted.', array('%label' => $label)));
    $form_state['redirect'] = 'admin/build/workflow';
  }
}


/**
 * View workflow permissions by role
 *
 * @param $workflow
 *   The workflow object.
 */
function workflow_permissions($workflow) {
  $map = array();
  $roles = workflow_get_roles();
  $transitions = $workflow->transitions;
  $states = $workflow->states;

  if (is_array($transitions)) {
    foreach ($transitions as $trn) {
      if (is_array($trn->roles)) {
        foreach ($trn->roles as $role) {
          $from = isset($states[$trn->state_name]) ? $states[$trn->state_name]->label : "";
          $to = isset($states[$trn->target_state_name]) ? $states[$trn->target_state_name]->label : "";
          $map[$role]['transitions'][] = array($from, WORKFLOW_ARROW, $to);
          $map[$role]['name'] = ucwords($roles[$role]);
        }
      }
    }
  }

  return theme('workflow_permissions', $map);
}

/**
 * Theme the workflow permissions view.
 */
function theme_workflow_permissions($map) {
  $header = array(t('From'), '', t('To'));
  $rows = array();

  foreach ($map as $role => $value) {
    $rows[] = array(
      'data' => array('<h3>'. t("%role may do these transitions:", array('%role' => $value['name'])) .'</h3>'),
      'colspan' => 3,
    );
    $rows = array_merge($rows, $value['transitions']);
  }

  return theme('table', $header, $rows);
}

/**
 * Menu callback. Edit a workflow's properties.
 *
 * @param $workflow
 *   The workflow object.
 * @return
 *   HTML form and permissions table.
 */
function workflow_edit_form(&$form_state, $workflow) {
  $form['#workflow'] = $workflow;
  $form['wid'] = array(
    '#type' => 'value',
    '#value' => $workflow->wid,
  );
  $form['name'] = array(
    '#type' => 'value',
    '#value' => $workflow->name,
  );
  $form['basic'] = array(
    '#type' => 'fieldset',
    '#title' => t('Workflow information'),
  );
  $form['basic']['label'] = array(
    '#type' => 'textfield',
    '#default_value' => $workflow->label,
    '#title' => t('Workflow Label'),
    '#size' => '16',
    '#maxlength' => '254',
  );
  $form['options'] = array(
    '#type' => 'fieldset',
    '#title' => t('Comment for Workflow Log'),
    '#tree' => TRUE,
  );
  $form['options']['comment_log_node'] = array(
    '#type' => 'checkbox',
    '#title' => t('Show a comment field in the workflow section of the editing form.'),
    '#default_value' => $workflow->options['comment_log_node'],
    '#description' => t("On the node editing form, a Comment form can be shown so that the person making the state change can record reasons for doing so. The comment is then included in the node's workflow history."),
  );
  $form['options']['comment_log_tab'] = array(
    '#type' => 'checkbox',
    '#title' => t('Show a comment field in the workflow section of the workflow tab form.'),
    '#default_value' => $workflow->options['comment_log_tab'],
    '#description' => t("On the workflow tab, a Comment form can be shown so that the person making the state change can record reasons for doing so. The comment is then included in the node's workflow history."),
  );
  $form['tab'] = array(
    '#type' => 'fieldset',
    '#title' => t('Workflow tab permissions'),
    '#collapsible' => TRUE,
    '#collapsed' => FALSE,
  );
  $form['tab']['tab_roles'] = array(
    '#type' => 'checkboxes',
    '#options' => workflow_get_roles(),
    '#default_value' => explode(',', $workflow->tab_roles),
    '#description' => t('Select any roles that should have access to the workflow tab on nodes that have a workflow.'),
  );

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save'),
  );

  return $form;
}

/**
 * Validate the workflow editing form.
 *
 * @see workflow_edit_form()
 */
function workflow_edit_form_validate($form, &$form_state) {
  $machine_name = $form_state['values']['name'];
  // Make sure workflow name is not a duplicate.
  if (!empty($form_state['values']['label'])) {
    $label = check_plain(t($form_state['values']['label']));
    foreach (workflow_load_all() as $name => $workflow) {
      if ($name != $machine_name && check_plain(t($label)) == $workflow->label) {
        form_set_error('label', t('A workflow with the label %label already exists. Please enter another label for this workflow.',
          array('%label' => $label)));
        break;
      }
    }
  }
  else {
    // No workflow name was provided.
    form_set_error('label', t('Please provide a nonblank label for this workflow.'));
  }
}

/**
 * Submit handler for the workflow editing form.
 *
 * @see workflow_edit_form()
 */
function workflow_edit_form_submit($form, &$form_state) {

  $values = $form_state['values'];
  $workflow = $form['#workflow'];
  if (is_array($values)) {
    foreach ($values as $key => $val) {
      if ($key == 'wid') continue;
      if (isset($workflow->$key)) {
        if ($key == "tab_roles") {
          $worfklow->roles = $val;        
          $val = implode(',', array_values($val));          
        }
        $workflow->$key = $val;
      }
    }
  }
  
  // Saving object from code
  if ($workflow->export_type == EXPORT_IN_CODE) {
    workflow_save_transitions($workflow);
  }
  workflow_save($workflow);
  drupal_set_message(t('The workflow was updated.'));
}

/**
 * Form builder. Build the grid of transitions for defining a workflow.
 *
 * @param $workflow
 *   The workflow object.
 */
function workflow_transition_grid_form(&$form_state, $workflow) {
  $form = array();
  $form['#workflow'] = $workflow;

  $transitions_matrix = workflow_transitions_to_matrix($workflow->transitions);

  $form['transitions'] = array(
    '#type' => 'fieldset',
    '#title' => t('Transitions'),
    '#collapsible' => TRUE,
    '#tree' => TRUE,
  );

  $roles = workflow_get_roles(); 
  $states = $workflow->states;
  if (empty($states)) {
    $form = array(
      '#type' => 'markup',
      '#value' => t('There are no states defined for this workflow.'),
    );
    return $form;
  }

  foreach ($states as $state) {
    foreach ($states as $nested_state) {
      if (workflow_is_system_state($nested_state->label)) {
        // Don't allow transition TO (creation).
        continue;
      }
      if ($nested_state->name != $state->name) {
        // Need to generate checkboxes for transition from $state to $nested_state.
        $from = $state;
        $to = $nested_state;
        foreach ($roles as $rid => $role_name) {
          $default_value = 0;
          if (isset($transitions_matrix[$from->name][$to->name])) {
            $default_value = in_array($rid, $transitions_matrix[$from->name][$to->name]);
          }
          $form['transitions'][$from->name][$to->name][$rid] = array(
            '#type' => 'checkbox',
            '#title' => check_plain($role_name),
            '#default_value' => $default_value,
          );
        }
      }
    }
  }

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save'),
  );

  $form['permissions'] = array(
    '#type' => 'fieldset',
    '#title' => t('Permissions Summary'),
    '#collapsible' => TRUE,
  );
  $form['permissions']['summary'] = array(
    '#value' => workflow_permissions($workflow),
  );

  return $form;
}

/**
* Convert an array of transition objects to a 2-dimensional array, more appropriate for quick lookups.
*/
function workflow_transitions_to_matrix($transitions) {
  $matrix = array();
  if (is_array($transitions)) {
    foreach ($transitions as $trn) {
      $matrix[$trn->state_name][$trn->target_state_name] = $trn->roles;      
    }
  }
  
  return $matrix;
  
}

/**
 * Validation handler for the transition_grid_form.
 *
 * @see workflow_transition_grid_form().
 */
function workflow_transition_grid_form_validate($form, &$form_state) {
  $workflow = $form['#workflow'];
  // Make sure 'author' is checked for (creation) -> [something].
  $creation_state = _workflow_creation_state($workflow->name);

  if (isset($form_state['values']['transitions'][$creation_state]) && is_array($form_state['values']['transitions'][$creation_state])) {
    foreach ($form_state['values']['transitions'][$creation_state] as $to => $roles) {
      if ($roles['author']) {
        $author_has_permission = TRUE;
        break;
      }
    }
  }
  $state_count = count($workflow->states);
  if (empty($author_has_permission) && $state_count > 1) {
    form_set_error('transitions', t('Please give the author permission to go from %creation to at least one state!',
      array('%creation' => '(creation)')));
  }
}

/**
 * Submit handler for the workflow editing form.
 *
 * @see workflow_transition_grid_form().
 */
function workflow_transition_grid_form_submit($form, &$form_state) {
  
  $workflow = $form['#workflow'];
  if ($workflow->export_type == WORKFLOW_IN_CODE) {
    //-- don't save stale transitions
    unset($workflow->transitions);
    workflow_save($workflow);
  }

  $values = $form_state['values']['transitions'];
  foreach ($values as $from_state => $transitions) {
    // Convert the roles array to the expected format.
    foreach ($transitions as $to_state => $roles) {
      $transitions[$to_state] = array_keys(array_filter($roles));
    }
    
    workflow_transitions_write($from_state, $transitions);
  }
  
  drupal_set_message(t('The workflow transitions were updated.'));
}

/**
 * Theme the workflow transitions form.
 *
 * @see workflow_edit_form()
 */
function theme_workflow_transition_grid_form($form) {
  $machine_name = $form['#workflow']->name;
  $states = $form['#workflow']->states;
  drupal_set_title(t('Edit Workflow %label Transitions', array('%label' => workflow_get_label($machine_name))));
  $output = '';

  if ($states) {
    $roles = workflow_get_roles();
    $header = array(array('data' => t('From / To ') . '&nbsp;' . WORKFLOW_ARROW));
    $rows = array();
    foreach ($states as $state) {
      // Don't allow transition TO (creation).
      if (!workflow_is_system_state($state->label)) {
        $header[] = array('data' => $state->label);
      }
      $row = array(array('data' => $state->label));
      foreach ($states as $nested_state) {
        if (workflow_is_system_state($nested_state->label)) {
          // Don't allow transition TO (creation).
          continue;
        }
        if ($nested_state->name != $state->name) {
          // Need to render checkboxes for transition from $state to $nested_state.
          $from = $state;
          $to = $nested_state;
          $cell = '';
          foreach ($roles as $rid => $role_name) {
            $cell .= drupal_render($form['transitions'][$from->name][$to->name][$rid]);
          }
          $row[] = array('data' => $cell);
        }
        else {
          $row[] = array('data' => '');
        }
      }
      $rows[] = $row;
    }
    $form['transitions']['#children'] .= theme('table', $header, $rows);
  }
  else {
    $output = t('There are no states defined for this workflow.');
  }

  // Pop the submit button in the transitions fieldset.
  $form['transitions']['#children'] .= drupal_render($form['submit']);

  $output .= drupal_render($form);
  return $output;
}

/**
 * Menu callback. Create the main workflow page, which gives an overview
 * of workflows and workflow states.
 */
function workflow_overview() {
  $row = array();
  foreach (workflow_load_all(TRUE) as $workflow_name => $workflow) {
    $links = array(
      'workflow_overview_actions' => array(
        'title' => t('Actions'),
        'href' => "admin/build/trigger/workflow/$workflow_name"),
      'workflow_overview_edit' => array(
        'title' => t('Edit'),
        'title_default' => t('Override'),
        'href' => "admin/build/workflow/edit/$workflow_name"),
      'workflow_overview_delete' => array(
        'title' => t('Delete'),
         // You can not delete a default workflow
        'title_default' => '',         
        'title_overridden' => t('Revert'),
        'href' => "admin/build/workflow/delete/$workflow_name",
         // You can not delete a default workflow
        'href_default' => "admin/build/workflow/edit/$workflow_name",
        'href_overridden' => "admin/build/workflow/revert/$workflow_name"),
    );

    // Allow modules to insert their own workflow operations.
    $links = array_merge($links, module_invoke_all('workflow_operations', 'workflow', $workflow->name));
    // Remove the Actions link if Trigger is not enabled or the Workflow has 
    // less than 2 states.
    if (!module_exists('trigger') || count($workflow->states) < 2) {
      unset($links['workflow_overview_actions']);
    }
    
    $row[] = array($workflow->label, 
                   $workflow->type,
                   theme('links',  workflow_overview_parse_links($links, $workflow)));
  }

  if ($row) {
    $output = theme('table', array(t('Workflow'), t('State'), t('Operations')), $row);
    $output .= drupal_get_form('workflow_types_form');
  }
  else {
    $output = '<p>' . t('No workflows have been added. Would you like to <a href="@link">add a workflow</a>?', array('@link' => url('admin/build/workflow/add'))) . '</p>';
  }

  return $output;
}


/**
* Pick appropriate link title and link href, depending on the state of the workflow (Normal, Default or Overridden).
*/
function workflow_overview_parse_links($links, $workflow) {

  $suffix = "";
  $suffix = strtolower($workflow->type);
 
  //-- Unfortunately, we can not just deduce $suffix from $workflow->type, because "type" gets translated and it does not work in non-English languages.
  if (($workflow->export_type & EXPORT_IN_CODE) && ($workflow->export_type & EXPORT_IN_DATABASE)) {
    $suffix = 'overridden';
  }
  elseif ($workflow->export_type == EXPORT_IN_CODE) {
    $suffix = 'default';
  }
  elseif ($workflow->export_type == EXPORT_IN_DATABASE) {
    $suffix = 'normal';
  }
   
  if (is_array($links)) {
    foreach ($links as &$link) {
      if (isset($link["title_" . $suffix])) {
        $link["title"] = $link["title_" . $suffix];
      }
      if (isset($link["href_" . $suffix])) {
        $link["href"] = $link["href_" . $suffix];
      }      
    }
  }
  
  return $links;
}


/**
 * Page callback for the Workflow States tab.
 */
function workflow_states($workflow) {
  return drupal_get_form('workflow_states_form_'. $workflow->name, $workflow);
}

/**
 * Form to re-order and create states for a specific workflow.
 *
 * @param $workflow
 *   The workflow object.
 */
function workflow_states_form(&$form_state, $workflow) {
  $form = array();
  $form['#workflow'] = $workflow;  
  
  $form['#states'] = $states = array_filter($workflow->states, 'workflow_state_is_active');
  $form['states']['#tree'] = TRUE;

  foreach ($states as $state_name => $state) {
    $form['states'][$state_name] = _workflow_state_edit_form($form_state, $state);
  }

  // Now add an empty state form.
  $form['states'][] = _workflow_state_create_form($form_state, $workflow->name);

  $form['buttons']['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Submit'),
  );

  // Since we're using custom form_ids (@see workflow_forms()) we need to 
  // manually specify some FAPI stuff.
  $form['buttons']['submit']['#submit'] = array('workflow_states_form_submit');
  $form['#validate'] = array('workflow_states_form_validate');
  $form['#theme'] = array('workflow_states_form_'. $workflow->name, 'workflow_states_form');

  return $form;
}

/**
 * Builds the fields for a single record on the drag-and-drop overview form.
 *
 * This internal function should not be called outside the module, unless you're
 * feeling particularly cheeky.
 *
 * @param $state
 *   The existing state object.
 * @return
 *   Form array.
 */
function _workflow_state_edit_form(&$form_state, $state) {
  
  $form['#state'] = $state;
  $form['workflow_name'] = array(
    '#type' => 'value',
    '#value' => $state->workflow_name,
  );
  $form['sid'] = array(
    '#type' => 'value',
    '#value' => $state->sid,
  );
   $form['name'] = array(
    '#type' => 'value',
    '#value' => $state->name,
  );

  $form['system_state'] = array(
    '#type' => 'value',
    '#value' => workflow_is_system_state($state->label),
  );

  // Only allow editing of the label on non-system states.
  if ($form['system_state']['#value']) {
    $form['label'] = array(
      '#type' => 'item',
      '#value' => $state->label,
    );
    $form['weight'] = array(
      '#type' => 'value',
      '#value' => $state->weight,
    );
  }
  else {
    $form['label'] = array(
      '#type'  => 'textfield',
      '#default_value' => empty($state->label) ? '' : $state->label,
      '#size'  => '16',
      '#maxlength' => '254',
      '#element_validate' => array('workflow_state_validate_label'),
    );
    $form['weight'] = array(
      '#type' => 'weight',
      '#title' => t('Weight'),
      '#default_value' => empty($state->weight) ? 0 : $state->weight,
    );
  }

  return $form;
}

/**
 * Builds the state creation form fields for the drag-and-drop overview form.
 *
 * This internal function should not be called outside the module, unless you're
 * feeling particularly cheeky.
 *
 * @param $workflow_name
 *   The machine name of the workflow.
 * @return
 *   Form array.
 */
function _workflow_state_create_form(&$form_state, $workflow_name) {
  $form = array();

  $form['workflow_name'] = array(
    '#type' => 'value',
    '#value' => $workflow_name,
  );

  $form['label'] = array(
    '#type'  => 'textfield',
    '#title' => t('State label'),
    '#size'  => '16',
    '#maxlength' => '254',
    '#description' => t('Enter the name for a state in your workflow. For example, if you were doing a meal workflow it may include states like <em>shop</em>, <em>prepare</em>, <em>eat</em>, and <em>clean up</em>.'),
    '#element_validate' => array('workflow_state_validate_label'),
  );

  // Only show the machine name field if this is a new state.
  $form['name'] = array(
    '#type'  => 'textfield',
    '#title' => t('Machine name'),
    '#size'  => '16',
    '#maxlength' => '254',
    '#field_prefix' =>   $workflow_name . '_',
    '#description' => t('Enter the machine name for this state. Must be unique and use only alphanumeric characters and underscores.'),
    '#element_validate' => array('workflow_state_validate_machine_name'),
  );

  $form['weight'] = array(
    '#type' => 'weight',
    '#title' => t('Weight'),
    '#default_value' => 0,
    '#description' => t('In listings, the heavier states will sink and the lighter states will be positioned nearer the top.'),
  );

  return $form;
}

/**
 * Theme the drag-and-drop overview form.
 *
 * Arranges records in a table, and adds the css and js for draggable sorting.
 *
 * @see workflow_states_form()
 */
function theme_workflow_states_form($form) {
  $rows = $cols = array();

  foreach (element_children($form['states']) as $state_name) {
    $state_form = &$form['states'][$state_name];
    $cols = array();
    
    // This is needed to prevent admin theme breaking tabledrag on forms due to its
    // special handling of description field.
    $cols[] = array('data' => '', 'style' => 'width:30px');

    // Render the hidden 'sid' field and the title of the record into the same 
    // column of the row, keeping in mind that 'sid' may be empty.
    $sid = empty($state_form['sid']) ? '' : drupal_render($state_form['sid']);
    $cols[] = $sid . drupal_render($state_form['label']);

    // If the weight field isn't disabled, add an identifying CSS class to it, 
    // as it's the one the tabledrag.js will be controlling.
    if (!$state_form['weight']['#disabled']) {
      $state_form['weight']['#attributes']['class'] = 'workflow-state-weight';
    }
    $cols[] = drupal_render($state_form['weight']);

    // The machine name field.
    $name_field = '<span class="machine-name">'. check_plain($state_form['name']['#value']) .'</span>';
    $cols[] = $name_field . drupal_render($state_form['name']);

    // Operations column, which is empty if this isn't an existing state.
    $cols[] = empty($form['#states'][$state_name]) ? '' : _workflow_state_links($form['#states'][$state_name]);

    // Add the new row to our collection of rows, and give it the 'draggable'
    // class if the row isn't a system state.
    $rows[] = array(
      'data' => $cols,
      'class' => $state_form['system_state']['#value'] ? '' : 'draggable',
    );
  }

  // Render a list of header titles, and our array of rows, into a table. Even
  // we've already rendered all of our records, we always call drupal_render()
  // on the form itself after we're done, so hidden security fields and other
  // elements (like buttons) will appear properly at the bottom of the form.
  $id = $form['#id'] .'-table';
  $header = array('', t('State Label'), t('Weight'), t('Machine Name'), t('Operations'));
  $form['states']['#children'] .= theme('table', $header, $rows, array('id' => $id));
  
  $form['states']['#children'] .= drupal_render($form['buttons']['submit']);
  $output = drupal_render($form);

  // Now that we've built our output, tell Drupal to add the tabledrag.js library.
  // We'll pass in the ID of the table, the behavior we want it to use, and the
  // class that appears on each 'weight' form element it should be controlling.
  drupal_add_tabledrag($id, 'order', 'sibling', 'workflow-state-weight');

  return $output;
}

/**
 * Build the edit and delete links for a single record.
 *
 * @see workflow_states_form()
 */
function _workflow_state_links($state) {
  $links = array();

  if (!workflow_is_system_state($state->label)) {
    $links['workflow_overview_delete_state'] = array(
      'title' => t('Disable'),
      'href' => "admin/build/workflow/state/disable/{$state->workflow_name}/{$state->name}",
      'query' => drupal_get_destination(),
    );
  }

  // Allow modules to insert state operations.
  $links = array_merge($links, module_invoke_all('workflow_operations', 'state', $workflow->name, $state->name));

  return theme('links', $links);
}

/**
 * Validate the state label.
 *
 * @see workflow_states_form()
 */
function workflow_state_validate_label($element, &$form_state) {
  $workflow_name = $form_state['values']['workflow_name'];
  $wf_states = workflow_get_states($workflow_name);
  $label = $element['#value'];

  // Unset this workflow state, if it exists. We detect the machine name from 
  // #parents... it's the second to last item in the array.
  $key = count($element['#parents']) - 2;
  $machine_name = $element['#parents'][$key];
  unset($wf_states[$machine_name]);

  // Make sure label is not already in use in this workflow.
  foreach ($wf_states as $wf_state) {
    if ($wf_state->label == check_plain(t($label))) {
      form_error($element, t('A state with the label %label already exists in this workflow. Please enter another label for your new state.', array('%label' => $label)));
      break;
    }
  }
}

/**
 * Validate the state machine name.
 *
 * @see workflow_states_form()
 */
function workflow_state_validate_machine_name($element, &$form_state) {
  $machine_name = strtolower($element['#value']);

  // Make sure machine name contains only alnum chars and underscores.
  if (preg_match("/[^[:alnum:]_]/", $machine_name)) {
    form_error($element, t("The machine name must consist of alphanumeric or underscore characters only."));
  }

  $states = $form_state['values']['states'];
  //-- If there are no other states, checking for duplicate is not needed
  if (!is_array($states) || sizeof($states)<1) return;
  
  $workflow_name = $states[0]['workflow_name'];
  
  // Ensure that this machine name is unique.
  if (workflow_state_machine_name_exists($workflow_name . '_' . $machine_name)) {
    form_error($element, t('A state with the machine name: %name in the workflow: %workflow already exists.', 
      array('%name' => $machine_name, '%workflow' => $workflow_name)));
  }
}

/**
 * Test to see if a state already exists with the requested machine name.
 *
 * @param $name
 *   The requested state machine name.
 * @return
 *   Boolean whether or not the machine name is already in use.
 */
function workflow_state_machine_name_exists($name) {
  static $names = array();

  if (empty($names)) {
    foreach (workflow_load_all() as $workflow) {
      foreach ($workflow->states as $state) {
        $names[] = $state->name;
      }
    }
  }

  return in_array($name, $names);
}

/**
 * Validate the states form.
 *
 * @see workflow_states_form()
 */
function workflow_states_form_validate($form, &$form_state) {
  foreach ($form_state['values']['states'] as $state_name => $state) {
    // No need to validate an empty form.
    if (!$state['system_state'] && (empty($state['name']) XOR empty($state['label']))) {
      $form_key = 'states]['. $state_name .'][';

      // Make sure the fields aren't empty.
      if (empty($state['label'])) {
        form_set_error($form_key .'label', t('You must specify a label for this state.'));
      }
      if (empty($state['name'])) {
        form_set_error($form_key .'name', t('You must specify a machine name for this state.'));
      }
    }
  }
}

/**
 * Submit handler for states form.
 *
 * @see workflow_states_form()
 */
function workflow_states_form_submit($form, &$form_state) {

  $states = $form_state['values']['states'];
  $workflow = $form['#workflow'];
  //-- don't save states from the code, though!
  unset($workflow->states); 

  //-- Let's make sure workflow is already in the database!  
  if ($workflow->export_type == WORKFLOW_IN_CODE) {
    workflow_save($workflow);
  }

  if (is_array($states)) {
    foreach ($states as $state_name => $state) {              
      // Ensure the state has all necessary fields and isn't a system state before saving.
      if (!empty($state['name']) && !empty($state['label']) && empty($state['system_state'])) {
        $state['name'] = strtolower($state['name']);

        //-- Prefix new state names with workflow-name to ensure they are unique-enough.
        //-- Caution: this check is specifically "isset()" since states in code would also
        //-- return TRUE on empty() but their "sid" is already set: to -1.
        //-- We do not want to add prefix to states from code, only to new states!
        if (!isset($state['sid'])) {
          $state['name'] = $workflow->name . '_' . $state['name'];
        }
        elseif ($state['sid'] == -1) {
          //unset -1 as sid for code-provided state so workflow_state_save does not go nuts
          unset($state['sid']);
          unset($states[$state_name]['sid']);
        }
        
        $state = workflow_state_save((object) $state);
        if (empty($states[$state_name]['sid'])) {
          watchdog('workflow', 'Created workflow state %label', array('%label' => $state->label));
          drupal_set_message(t('The workflow state %label was created.', array('%label' => $state->label)));
        }
      }
    }
  }
  workflow_clear_cache();
}

/**
 * Form for confirmation of deleting a workflow state.
 *
 * @param $workflow
 *   The workflow object.
 * @param $state_name
 *   The machine name of the state to delete.
 * @return
 *   Confirmation form array.
 */
function workflow_state_disable_form($form_state, $workflow, $state_name) {
  $form['#workflow'] = $workflow;
  $label = $workflow->states[$state_name]->label;

  // Don't allow users to delete a system state.
  if (workflow_is_system_state($label)) {
    return drupal_access_denied();
  }

  // Will any nodes have no state if this state is deleted?
  if ($count = db_result(db_query("SELECT COUNT(nid) FROM {workflow_node} WHERE state_name = '%s'", $state_name))) {
    // Cannot assign a node to (creation) because that implies
    // that the node does not exist.
    foreach ($states as $key => $value) {
      if ($value->label == t('(creation)')) {
        unset($states[$key]);
      }
    }

    // Don't include the state to be deleted in our list of possible
    // states that can be assigned.
    unset($states[$state_name]);
    if ($states) {
      $form['new_state'] = array(
        '#type' => 'select',
        '#title' => t('State to be assigned to orphaned nodes'),
        '#description' => format_plural($count, 'Since you are disabling a workflow state, a node which is in that state will be orphaned, and must be reassigned to a new state. Please choose the new state.', 'Since you are disabling a workflow state, @count nodes which are in that state will be orphaned, and must be reassigned to a new state. Please choose the new state.'),
        '#options' => $states,
      );
    }
    else {
      $form['warning'] = array(
        '#value' => format_plural($count, 'Since you are disabling the last workflow state in this workflow, the one remaining node which is in that state will have its workflow state removed. ', 'Since you are disabling the last workflow state in this workflow, @count nodes which are in that state will have their workflow state removed. '),
      );
    }
  }

  $form['workflow_name'] = array(
    '#type' => 'value',
    '#value' => $workflow->name,
  );
  $form['name'] = array(
    '#type' => 'value',
    '#value' => $state_name,
  );

  return confirm_form(
    $form,
    t('Are you sure you want to disable %title (and all its transitions)?', array('%title' => $label)),
    !empty($_GET['destination']) ? $_GET['destination'] : 'admin/build/workflow',
    t('This action cannot be undone.'),
    t('Disable'),
    t('Cancel')
  );
}

/**
 * Submit handler for workflow state deletion form.
 *
 * @see workflow_state_disable_form()
 */
function workflow_state_disable_form_submit($form, &$form_state) {
  $workflow = $form['#workflow'];
  $states = $workflow->states;
  $state_name = $form_state['values']['name'];
  $label = $states[$state_name]->label;

  if ($form_state['values']['confirm'] == 1) {
    if ($workflow->export_type === WORKFLOW_IN_CODE) {
      workflow_save($workflow);
    }
    $new_state = isset($form_state['values']['new_state']) ? $form_state['values']['new_state'] : NULL;
    workflow_state_delete($state_name, $new_state);
    watchdog('workflow', 'Disabled workflow state %name', array('%name' => $label));
    drupal_set_message(t('The workflow state %name was disabled.', array('%name' => $label)));
    workflow_invalidate_cache();
  }
}

/**
 * Form builder. Allow administrator to map workflows to content types
 * and determine placement.
 */
function workflow_types_form() {
  $form = array();
  $workflows = workflow_load_all_labels();
  if (count($workflows) == 0) {
    return $form;
  }

  $options = array('<'. t('None') .'>') + $workflows;
  
  $type_map = workflow_node_type_map_load();

  $form['#theme'] = 'workflow_types_form';
  $form['#tree'] = TRUE;
  $form['help'] = array(
    '#type' => 'item',
    '#value' => t('Each content type may have a separate workflow. The form for changing workflow state can be displayed when editing a node, editing a comment for a node, or both.'),
  );

  $form['#node_types'] = $node_types = node_get_types('names');

  foreach ($node_types as $type => $name) {
    $form[$type]['workflow_name'] = array(
      '#type' => 'select',
      '#title' => check_plain($name),
      '#options' => $options,
      '#default_value' => isset($type_map[$type]) ? $type_map[$type] : NULL,
    );
    $form[$type]['placement'] = array(
      '#type' => 'checkboxes',
      '#options' => array(
        'node' => t('Post'),
        'comment' => t('Comment'),
      ),
      '#default_value' => variable_get('workflow_' . $type, array('node')),
    );
  }
  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save workflow mapping'),
  );
  return $form;
}

/**
 * Theme the workflow type mapping form.
 */
function theme_workflow_types_form($form) {
  $header = array(t('Content Type'), t('Workflow'), t('Display Workflow Form for:'));
  $rows = array();

  $node_types = $form['#node_types'];

  foreach ($node_types as $type => $name) {
    $name = $form[$type]['workflow_name']['#title'];
    unset($form[$type]['workflow_name']['#title']);
    $rows[] = array($name, drupal_render($form[$type]['workflow_name']), drupal_render($form[$type]['placement']));
  }
  $output = drupal_render($form['help']);
  $output .= theme('table', $header, $rows);
  return $output . drupal_render($form);
}

/**
 * Submit handler for workflow type mapping form.
 *
 * @see workflow_types_form()
 */
function workflow_types_form_submit($form, &$form_state) {
  
  $node_types = $form['#node_types'];

  foreach ($node_types as $type => $name) {
    //-- now that type mapping is exportable, we need to save mappings with workflow_name = none, too
    //-- otherwise they won't be able to override mappings from code and you will never
    //-- be able to disable a mapping provided from code.
      $workflow_name = $form_state['values'][$type]['workflow_name'];
      workflow_node_type_save($workflow_name, $type);
      variable_set('workflow_' . $type, array_keys(array_filter($form_state['values'][$type]['placement'])));    
  }

  drupal_set_message(t('The workflow mapping was saved.'));
  menu_rebuild();
  workflow_invalidate_cache();
  $form_state['redirect'] = 'admin/build/workflow';
}